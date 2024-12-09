#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "commands.h"
#include "fat16.h"
#include "support.h"

#include <errno.h>
#include <err.h>
#include <error.h>
#include <assert.h>

#include <sys/types.h>

/*
 * Função para realizar a busca na pasta raiz.
 * A função percorre todas as bpb->possible_rentries no struct fat_dir* dirs
 * e retorna a primeira entrada cujo nome corresponde ao filename.
 */

struct far_dir_searchres find_in_root(struct fat_dir* root, char* filename, struct fat_bpb* bpb)
{
    struct far_dir_searchres result = { .found = false, .fdir = NULL, .idx = 0 };

    for (uint32_t i = 0; i < bpb->root_entry_count; i++)
    {
        if (strncmp((char*)root[i].name, filename, FAT16STR_SIZE) == 0)
        {
            result.found = true;
            result.fdir = root[i];
            result.idx = i;
            break;
        }
    }

    return result;
}

/*
 * A função percorre todas as bpb->possible_rentries do diretório raiz
 * e lê os dados utilizando a função read_bytes().
 */

struct fat_dir *ls(FILE *fp, struct fat_bpb *bpb)
{
    // Verifica se é FAT32
    if (bpb->sect_per_fat_16 != 0) {
        error_at_line(EXIT_FAILURE, EINVAL, __FILE__, __LINE__, "O sistema de arquivos não é FAT32");
        return NULL;
    }

    // Calcula o endereço do diretório raiz
    uint32_t root_cluster = bpb->root_cluster & FAT32_CLUSTER_MASK;
    uint32_t root_address = bpb_fdata_addr(bpb) + (root_cluster - 2) * bpb->sector_p_clust * bpb->bytes_p_sect;

    // Tenta ler o diretório raiz (Tamanho de um cluster, por exemplo)
    uint32_t cluster_size = bpb->sector_p_clust * bpb->bytes_p_sect;
    struct fat_dir *dirs = malloc(cluster_size);

    if (!dirs) {
        error_at_line(EXIT_FAILURE, ENOMEM, __FILE__, __LINE__, "Falha ao alocar memória para o diretório");
    }

    if (read_bytes(fp, root_address, dirs, cluster_size) == RB_ERROR) {
        free(dirs);
        error_at_line(EXIT_FAILURE, EIO, __FILE__, __LINE__, "Erro ao ler struct fat_dir");
    }

    return dirs;
}

void mv(FILE *fp, char *source, char* dest, struct fat_bpb *bpb)
{
    char source_rname[FAT16STR_SIZE_WNULL], dest_rname[FAT16STR_SIZE_WNULL];

    // FORMATAÇÃO DOS NOMES
    bool badname = cstr_to_fat16wnull(source, source_rname) || cstr_to_fat16wnull(dest, dest_rname);

    if (badname)
    {
        fprintf(stderr, "Nome de arquivo inválido.\n");
        exit(EXIT_FAILURE);
    }

    // CALCULO DE TAMANHO E ENDEREÇO DO DIRETÓRIO RAIZ
    uint32_t root_address = bpb_froot_addr(bpb);
    uint32_t root_size = sizeof(struct fat_dir) * bpb->root_entry_count;

    // LEITURA DO DIRETÓRIO RAIZ
    struct fat_dir root[root_size];
    if (read_bytes(fp, root_address, &root, root_size) == RB_ERROR)
    {
        error_at_line(EXIT_FAILURE, EIO, __FILE__, __LINE__, "erro ao ler struct fat_dir");
    }

    // BUSCA DOS ARQUIVOS
    struct far_dir_searchres dir1 = find_in_root(root, source_rname, bpb);
    struct far_dir_searchres dir2 = find_in_root(root, dest_rname, bpb);

    // VERIFICAÇÕES DE EXISTENCIA DE ARQUIVO/ARQUIVO ORIGEM
    if (dir2.found == true)
    {
        error(EXIT_FAILURE, 0, "Não permitido substituir arquivo %s via mv.", dest);
    }
    if (dir1.found == false)
    {
        error(EXIT_FAILURE, 0, "Não foi possivel encontrar o arquivo %s.", source);
    }

    //RENAME ARQUIVO ORIGEM
    memcpy(dir1.fdir.name, dest_rname, sizeof(char) * FAT16STR_SIZE);

    //CALCULO ENDEREÇO DE ENTRADA DO DIRETÓRIO
    uint32_t source_address = root_address + dir1.idx * sizeof(struct fat_dir);

    // ESCREVER NO DISCO 
    if (fseek(fp, source_address, SEEK_SET) != 0)
    {
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__, "erro ao posicionar o ponteiro do arquivo");
    }

    if (fwrite(&dir1.fdir, sizeof(struct fat_dir), 1, fp) != 1)
    {
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__, "erro ao escrever a entrada do diretório");
    }

    printf("mv %s → %s.\n", source, dest);
}

void rm(FILE* fp, char* filename, struct fat_bpb* bpb)
{
    char fat16_rname[FAT16STR_SIZE_WNULL];

    // FORMATAÇÃO DE NOMES
    if (cstr_to_fat16wnull(filename, fat16_rname))
    {
        fprintf(stderr, "Nome de arquivo inválido.\n");
        exit(EXIT_FAILURE);
    }

    //DETERMINAÇÃO ENDEREÇO DIRETÓRIO RAIZ E TAMANHO
    uint32_t root_address = bpb_froot_addr(bpb);
    uint32_t root_size = sizeof(struct fat_dir) * bpb->root_entry_count;

    // LEITURA DO DIRETÓRIO RAIZ
    struct fat_dir root[root_size];
    if (read_bytes(fp, root_address, &root, root_size) == RB_ERROR)
    {
        error_at_line(EXIT_FAILURE, EIO, __FILE__, __LINE__, "erro ao ler struct fat_dir");
    }

    // BUSCA DO ARQUIVO
    struct far_dir_searchres dir = find_in_root(&root[0], fat16_rname, bpb);

    // VERIFICAÇÃO DE EXISTENCIA DO ARQUIVO
    if (dir.found == false)
    {
        fprintf(stderr, "Arquivo não encontrado.\n");
        return;
    }

    // MARCAÇÃO DE ENTRADA DE DIRETÓRIO COMO LIVRE
    dir.fdir.name[0] = DIR_FREE_ENTRY;

    // CALCULO ENDEREÇO DE ENTRADA DIRETÓRIO
    uint32_t file_address = root_address + dir.idx * sizeof(struct fat_dir);

    // ESCRITA NO DISCO
    if (fseek(fp, file_address, SEEK_SET) != 0)
    {
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__, "erro ao posicionar o ponteiro do arquivo");
    }

    if (fwrite(&dir.fdir, sizeof(struct fat_dir), 1, fp) != 1)
    {
        error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__, "erro ao escrever a entrada do diretório");
    }

    printf("Arquivo %s removido.\n", filename);
}

struct fat16_newcluster_info fat16_find_free_cluster(FILE* fp, struct fat_bpb* bpb)
{
    uint16_t cluster = 0x0;
    uint32_t fat_address = bpb_faddress(bpb);
    uint32_t total_clusters = bpb_fdata_cluster_count(bpb);

    for (cluster = 0x2; cluster < total_clusters; cluster++)
    {
        uint32_t entry_address = fat_address + cluster * sizeof(uint16_t);
        uint16_t entry;

        if (read_bytes(fp, entry_address, &entry, sizeof(uint16_t)) == RB_ERROR)
        {
            error_at_line(EXIT_FAILURE, errno, __FILE__, __LINE__, "erro ao ler a entrada da FAT");
        }

        if (entry == 0x0)
        {
            return (struct fat16_newcluster_info) { .cluster = cluster, .address = entry_address };
        }
    }

    return (struct fat16_newcluster_info) { .cluster = 0, .address = 0 };
}

void cp(FILE *fp, char* source, char* dest, struct fat_bpb *bpb)
{
     char source_rname[FAT16STR_SIZE_WNULL], dest_rname[FAT16STR_SIZE_WNULL];

    // FORMATAÇÃO DOS NOMES
    bool badname = cstr_to_fat16wnull(source, source_rname) || cstr_to_fat16wnull(dest, dest_rname);

    if (badname)
    {
        fprintf(stderr, "Nome de arquivo inválido.\n");
        exit(EXIT_FAILURE);
    }

    // CALCULO DE TAMANHO E ENDEREÇO DO DIRETÓRIO RAIZ
    uint32_t root_address = bpb_froot_addr(bpb);
    uint32_t root_size = sizeof(struct fat_dir) * bpb->root_entry_count;

    // LEITURA DO DIRETÓRIO RAIZ
    struct fat_dir root[root_size];
    if (read_bytes(fp, root_address, &root, root_size) == RB_ERROR)
    {
        error_at_line(EXIT_FAILURE, EIO, __FILE__, __LINE__, "erro ao ler struct fat_dir");
    }

    struct far_dir_searchres dir1 = find_in_root(root, source_rname, bpb);

    if (!dir1.found)
        error(EXIT_FAILURE, 0, "Não foi possível encontrar o arquivo %s.", source);

    if (find_in_root(root, dest_rname, bpb).found)
        error(EXIT_FAILURE, 0, "Não permitido substituir arquivo %s via cp.", dest);

    struct fat_dir new_dir = dir1.fdir;
    memcpy(new_dir.name, dest_rname, FAT16STR_SIZE);
    bool dentry_failure = true;

    //BUSCA DE ENTRADA LIVRE EM DIRETÓRIO RAIZ
    for (int i = 0; i < bpb->root_entry_count; i++) if (root[i].name[0] == DIR_FREE_ENTRY || root[i].name[0] == '\0')
    {
        //CALCULO ENDEREÇO FINAL
        uint32_t dest_address = sizeof (struct fat_dir) * i + root_address;

        //APLICAÇÃO NEW_DIR EM DIRETÓRIO RAIZ (NOVO DIRETÓRIO)
        (void) fseek (fp, dest_address, SEEK_SET);
        (void) fwrite
        (
            &new_dir,
            sizeof (struct fat_dir),
            1,
            fp
        );

        dentry_failure = false;

        break;
    }

    if (dentry_failure)
        error_at_line(EXIT_FAILURE, ENOSPC, __FILE__, __LINE__, "Não foi possivel alocar uma entrada no diretório raiz.");

    //ALOCAÇÃO DE CLUSTERS PARA NOVO ARQUIVO
    // Contador de clusters
    int count = 0;

    //CLUSTERS
    {
        /*
         * Detalhes sobre a alocação de novos clusters:S
         * A alocação dos clusters ocorre de trás para frente; o último é alocado primeiro,
         * principalmente para garantir que seu valor seja FAT16_EOF_HI.
        */
        struct fat16_newcluster_info next_cluster, prev_cluster = { .cluster = FAT16_EOF_HI };
        //NUMERO DE CLUSTERS QUE O ARQUIVO PRECISA 
        uint32_t cluster_count = dir1.fdir.file_size / bpb->bytes_p_sect / bpb->sector_p_clust + 1;
        // ALOCA-SE CLUSTERS GRAVANDO-O NA FAT
        while (cluster_count--)
        {
            prev_cluster = next_cluster;
            next_cluster = fat16_find_free_cluster(fp, bpb); /* Busca-se novo cluster */

            if (next_cluster.cluster == 0x0)
                error_at_line(EXIT_FAILURE, EIO, __FILE__, __LINE__, "Disco cheio (imagem foi corrompida)");

            (void) fseek (fp, next_cluster.address, SEEK_SET);
            (void) fwrite(&prev_cluster.cluster, sizeof (uint16_t), 1, fp);

            count++;
        }

        //CLUSTER DE INICIO SALVO NO DIRETÓRIO
        new_dir.starting_cluster = next_cluster.cluster;
    }

    {
        /* MESMA LÓGICA QUE USADA NO MÉTODO cat() */
        const uint32_t fat_address       = bpb_faddress(bpb);
        const uint32_t data_region_start = bpb_fdata_addr(bpb);
        const uint32_t cluster_width     = bpb->bytes_p_sect * bpb->sector_p_clust;

        size_t bytes_to_copy = new_dir.file_size;

        /* AMBOS OS ARQUIVOS, FONTE E DESTINO, SÃO ITERADOS SIMULTANEAMENTE*/
        uint16_t source_cluster_number = dir1.fdir.starting_cluster;
        uint16_t destin_cluster_number = new_dir.starting_cluster;

        while (bytes_to_copy != 0)
        {
            uint32_t source_cluster_address = (source_cluster_number - 2) * cluster_width + data_region_start;
            uint32_t destin_cluster_address = (destin_cluster_number - 2) * cluster_width + data_region_start;

            size_t copied_in_this_sector = MIN(bytes_to_copy, cluster_width);

            char filedata[cluster_width];

            // LÊ DA FONTE E ESCREVE NO DESTINO
            (void) read_bytes(fp, source_cluster_address, filedata, copied_in_this_sector);
            (void) fseek     (fp, destin_cluster_address, SEEK_SET);
            (void) fwrite    (filedata, sizeof (char), copied_in_this_sector, fp);

            bytes_to_copy -= copied_in_this_sector;

            uint32_t source_next_cluster_address = fat_address + source_cluster_number * sizeof (uint16_t);
            uint32_t destin_next_cluster_address = fat_address + destin_cluster_number * sizeof (uint16_t);

            (void) read_bytes(fp, source_next_cluster_address, &source_cluster_number, sizeof (uint16_t));
            (void) read_bytes(fp, destin_next_cluster_address, &destin_cluster_number, sizeof (uint16_t));
        }
    }

    printf("cp %s → %s, %i clusters copiados.\n", source, dest, count);

    return;
}

void cat(FILE* fp, char* filename, struct fat_bpb* bpb)
{
    //LEITURA/ MANIPUÇ DIRETÓRIO RAIZ  

    char rname[FAT16STR_SIZE_WNULL];

    bool badname = cstr_to_fat16wnull(filename, rname);

    if (badname)
    {
        fprintf(stderr, "Nome de arquivo inválido.\n");
        exit(EXIT_FAILURE);
    }

    uint32_t root_address = bpb_froot_addr(bpb);
    uint32_t root_size    = sizeof (struct fat_dir) * bpb->root_entry_count;

    struct fat_dir root[root_size];

    if (read_bytes(fp, root_address, &root, root_size) == RB_ERROR)
        error_at_line(EXIT_FAILURE, EIO, __FILE__, __LINE__, "erro ao ler struct fat_dir");

    struct far_dir_searchres dir = find_in_root(&root[0], rname, bpb);

    if (dir.found == false)
        error(EXIT_FAILURE, 0, "Não foi possivel encontrar o %s.", filename);

    // DETERMINAÇÃO TAMANHO DO ARQUIVO
    size_t   bytes_to_read     = dir.fdir.file_size;

    // ENDEREÇO REGIÃO DE DADOS // INICIO REGIÃO DE DADOS 
    uint32_t data_region_start = bpb_fdata_addr(bpb);
    uint32_t fat_address       = bpb_faddress(bpb);

    // LEITURA DE CLUSTERS // PRIMEIRO CLUSTER ESTÁ 
    uint16_t cluster_number    = dir.fdir.starting_cluster;

    const uint32_t cluster_width = bpb->bytes_p_sect * bpb->sector_p_clust;

    while (bytes_to_read != 0)
    {
        /* LEITURA */
        {
            // PROCURA ONDE ESTÁ CLUSTAR ATUAL EM DISCO
            uint32_t cluster_address     = (cluster_number - 2) * cluster_width + data_region_start;

            // QUANTOS BYTES LER NESSE CLUSTER
            size_t read_in_this_sector = MIN(bytes_to_read, cluster_width);

            char filedata[cluster_width];

            // LEITURA CLUSTER ATUAL
            read_bytes(fp, cluster_address, filedata, read_in_this_sector);
            printf("%.*s", (signed) read_in_this_sector, filedata);

            bytes_to_read -= read_in_this_sector;
        }

        // CALCULO DE ENDEREÇO TABELA DE ALOCAÇÃO / LOCALIZAÇÃO DO PRÓXIMO CLUSTER
        uint32_t next_cluster_address = fat_address + cluster_number * sizeof (uint16_t);

        //LER ENTRADA/ DESCOBRIR QUAL É O PRÓXIMO CLUSTER
        read_bytes(fp, next_cluster_address, &cluster_number, sizeof (uint16_t));
    }

    return;
}