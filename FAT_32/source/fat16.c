#include "fat16.h"
#include <stdlib.h>
#include <errno.h>
#include <error.h>
#include <err.h>

#define FAT32_CLUSTER_MASK 0x0FFFFFFF // Máscara para considerar apenas os 28 bits alocados

/* Calcula o endereço inicial da FAT */
uint32_t bpb_faddress(struct fat_bpb *bpb)
{
    // A área reservada contém [bytes_p_sect] bytes, multiplica por [reserved_sect] para obter o endereço inicial da FAT
    return bpb->reserved_sect * bpb->bytes_p_sect;
}

/* Calcula o endereço do diretório raiz */
uint32_t bpb_froot_addr(struct fat_bpb *bpb)
{
    // FAT32: Aplica a máscara ao cluster inicial
    uint32_t root_cluster = bpb->root_cluster & FAT32_CLUSTER_MASK;
    return bpb_fdata_addr(bpb) + (root_cluster - 2) * bpb->sector_p_clust * bpb->bytes_p_sect;
}

/* Calculo do endereço inicial dos dados */
uint32_t bpb_fdata_addr(struct fat_bpb *bpb)
{
    // FAT32
    return bpb_faddress(bpb) + (bpb->n_fat * bpb->sect_per_fat_32 * bpb->bytes_p_sect);
}

/* Calcula a quantidade de setores/blocos de dados (Um setor contém muitos bytes de um arquivo até um limite) */
uint32_t bpb_fdata_sector_count(struct fat_bpb *bpb)
{
    uint32_t total_sectors = bpb->total_sectors_32; // Para FAT32, utiliza-se total_sectors_32
    uint32_t fat_size = bpb->sect_per_fat_32;       // Para FAT32, utiliza-se sect_per_fat_32
    uint32_t data_sectors = total_sectors - (bpb->reserved_sect + (bpb->n_fat * fat_size));
    return data_sectors;
}

/* Calcula a quantidade de setores/blocos de dados (Um setor contém muitos bytes de um arquivo até um limite) */
static uint32_t bpb_fdata_sector_count_s(struct fat_bpb* bpb)
{
    uint32_t total_sectors = bpb->total_sectors_32; // Para FAT32
    return total_sectors - bpb_fdata_addr(bpb) / bpb->bytes_p_sect;
}

/* Calcula a quantidade de clusters de dados (Um cluster contém um ou mais setores) */
uint32_t bpb_fdata_cluster_count(struct fat_bpb *bpb)
{
    uint32_t data_sectors = bpb_fdata_sector_count_s(bpb);
    return data_sectors / bpb->sector_p_clust; // Clusters não precisam de máscara aqui
}

/*
 * allows reading from a specific offset and writing the data to buff
 * returns RB_ERROR if seeking or reading failed and RB_OK if success
 */
int read_bytes(FILE *fp, unsigned int offset, void *buff, unsigned int len)
{
    if (fseek(fp, offset, SEEK_SET) != 0)
    {
        error_at_line(0, errno, __FILE__, __LINE__, "warning: error when seeking to %u", offset);
        return RB_ERROR;
    }
    if (fread(buff, 1, len, fp) != len)
    {
        error_at_line(0, errno, __FILE__, __LINE__, "warning: error reading file");
        return RB_ERROR;
    }

    return RB_OK;
}

/* read FAT32's bios parameter block */
void rfat(FILE *fp, struct fat_bpb *bpb)
{
    read_bytes(fp, 0x0, bpb, sizeof(struct fat_bpb));

    // Verifica se é FAT32
    if (bpb->sect_per_fat_16 != 0 || bpb->sect_per_fat_32 == 0) {
        fprintf(stderr, "Erro: O sistema de arquivos não é FAT32.\n");
        exit(EXIT_FAILURE);
    }
}
