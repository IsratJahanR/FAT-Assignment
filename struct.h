#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <stdint.h>

typedef struct
{
    char DIR_Name[11];
    uint8_t DIR_Attr;
    uint8_t DIR_NTRes;
    uint8_t DIR_CrtTimeTenth;
    uint16_t DIR_CrtTime;
    uint16_t DIR_CrtDate;
    uint16_t DIR_LstAccDate;
    uint16_t DIR_FstClusHI;
    uint16_t DIR_WrtTime;
    uint16_t DIR_WrtDate;
    uint16_t DIR_FstClusLO;
    uint32_t DIR_FileSize;
} __attribute__((packed)) DirEntry;

typedef struct {
   uint8_t jumpInstruction[3];
   uint8_t oemName[8];
   uint16_t bytesPerSector;
   uint8_t sectorsPerCluster;
   uint16_t reservedSectorCount;
   uint8_t numberOfFATs;
   uint16_t rootEntryCount;
   uint16_t totalSectors16;
   uint8_t mediaDescriptor;
   uint16_t fatSize16;
   uint16_t sectorsPerTrack;
   uint16_t numberOfHeads;
   uint32_t hiddenSectors;
   uint32_t totalSectors32;
   uint32_t fatSize32;
   uint16_t extendedFlags;
   uint16_t fileSystemVersion;
   uint32_t rootCluster;
   uint16_t fsInfoSector;
   uint16_t backupBootSector;
   uint8_t reserved[12];
   uint8_t driveNumber;
} __attribute__((packed)) BootSector;

extern FILE *disk_img;
extern BootSector bs;
extern char current_image_name[Mx_FILENAME_LENGTH];
extern uint32_t current_dir_cluster;

#endif 