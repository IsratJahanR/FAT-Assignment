#include "mfs.h"
#include "struct.h"

uint32_t current_dir_cluster;

FILE *disk_img = NULL;
BootSector bs;
char current_image_name[Mx_FILENAME_LENGTH];

int open_filesystem(const char *imageFilename);
void close_filesystem(void);
void display_filesystem_info(void);
void execute_command(char *commandLine);

uint32_t get_first_sector_of_cluster(uint32_t clusterNumber);
uint32_t get_fat_entry(uint32_t clusterNumber);
void convert_to_fat_filename(const char *input, char *output);
DirEntry *find_file_entry_entry(const char *filename, uint32_t directoryCluster);
void list_directory_entries(uint32_t clusterNumber);
int read_disk_sector(uint32_t sector, void *buffer);
int write_disk_sector(uint32_t sector, const void *buffer);
void upload_file(const char *sourceFile, const char *newFilename);

void update_fat_entry(uint32_t clusterNumber, uint32_t value);
void read_file_content(const char *filename, uint32_t startPosition, uint32_t byteCount, int format);
void delete_file(const char *filename);
void restore_deleted_file(const char *filename);
DirEntry* find_deleted_file_entry(const char* filename, uint32_t clusterNumber, uint32_t* sectorNum, int* entryIdx, int includeDeleted);


void update_fat_entry(uint32_t cluster, uint32_t value)
{
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = bs.reservedSectorCount + (fat_offset / bs.bytesPerSector);
    uint32_t ent_offset = fat_offset % bs.bytesPerSector;

    uint8_t buffer[SECTOR_SIZE];

    if (read_disk_sector(fat_sector, buffer) != 1)
    {
        return;
    }

    uint32_t *fat_entry = (uint32_t *)(&buffer[ent_offset]);
    *fat_entry = (*fat_entry & 0xF0000000) | (value & 0x0FFFFFFF);

    for (int i = 0; i < bs.numberOfFATs; i++)
    {
        uint32_t current_fat_sector = fat_sector + (i * bs.fatSize32);
        write_disk_sector(current_fat_sector, buffer);
    }
}

uint32_t get_first_sector_of_cluster(uint32_t cluster)
{
    uint32_t first_data_sector = bs.reservedSectorCount + (bs.numberOfFATs * bs.fatSize32);
    return first_data_sector + ((cluster - 2) * bs.sectorsPerCluster);
}

uint32_t get_fat_entry(uint32_t cluster)
{
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = bs.reservedSectorCount + (fat_offset / bs.bytesPerSector);
    uint32_t ent_offset = fat_offset % bs.bytesPerSector;

    uint8_t buffer[SECTOR_SIZE];
    read_disk_sector(fat_sector, buffer);

    uint32_t entry = *(uint32_t *)(&buffer[ent_offset]);
    return entry & 0x0FFFFFFF;
}

void convert_to_fat_filename(const char *input, char *expanded)
{
    memset(expanded, ' ', 11);
    expanded[11] = '\0';

    char *token = strtok((char *)input, ".");
    if (token)
    {
        int len = strlen(token);
        if (len > 8)
            len = 8;
        memcpy(expanded, token, len);

        token = strtok(NULL, ".");
        if (token)
        {
            len = strlen(token);
            if (len > 3)
                len = 3;
            memcpy(expanded + 8, token, len);
        }
    }

    for (int i = 0; i < 11; i++)
    {
        expanded[i] = toupper(expanded[i]);
    }
}

int read_disk_sector(uint32_t sector, void *buffer)
{
    if (!disk_img)
        return -1;
    fseek(disk_img, sector * bs.bytesPerSector, SEEK_SET);
    return fread(buffer, bs.bytesPerSector, 1, disk_img);
}

int write_disk_sector(uint32_t sector, const void *buffer)
{
    if (!disk_img)
        return -1;
    fseek(disk_img, sector * bs.bytesPerSector, SEEK_SET);
    return fwrite(buffer, bs.bytesPerSector, 1, disk_img);
}

DirEntry *find_file_entry(const char *filename, uint32_t dir_cluster)
{
    static DirEntry dir_entry;
    static uint8_t buffer[SECTOR_SIZE];

    uint32_t sector = get_first_sector_of_cluster(dir_cluster);

    while (1)
    {
        read_disk_sector(sector, buffer);
        DirEntry *dir = (DirEntry *)buffer;

        for (int i = 0; i < bs.bytesPerSector / sizeof(DirEntry); i++)
        {
            if (dir[i].DIR_Name[0] == 0x00)
                return NULL; 
            if (dir[i].DIR_Name[0] == 0xE5)
                continue; 

            if (strncmp((char *)dir[i].DIR_Name, filename, 11) == 0)
            {
                memcpy(&dir_entry, &dir[i], sizeof(DirEntry));
                return &dir_entry;
            }
        }

        uint32_t next_cluster = get_fat_entry(dir_cluster);
        if (next_cluster >= EOC)
            break;

        dir_cluster = next_cluster;
        sector = get_first_sector_of_cluster(dir_cluster);
    }

    return NULL;
}

DirEntry* find_deleted_file_entry(const char* filename, uint32_t cluster, uint32_t* sector_num, int* entry_index, int include_deleted) {
    static DirEntry dir_entry;
    static uint8_t buffer[SECTOR_SIZE];
    
    while (1) {
        uint32_t sector = get_first_sector_of_cluster(cluster);
        for (uint32_t i = 0; i < bs.sectorsPerCluster; i++) {
            if (read_disk_sector(sector + i, buffer) != 1) {
                return NULL;
            }

            DirEntry* dir = (DirEntry*)buffer;
            for (int j = 0; j < bs.bytesPerSector / sizeof(DirEntry); j++) {
                if (dir[j].DIR_Name[0] == 0x00) {
                    return NULL;
                }

                if (dir[j].DIR_Name[0] == 0xE5 && include_deleted) {
                    if (memcmp(dir[j].DIR_Name + 1, filename + 1, 10) == 0) {
                        if (sector_num) *sector_num = sector + i;
                        if (entry_index) *entry_index = j;
                        memcpy(&dir_entry, &dir[j], sizeof(DirEntry));
                        return &dir_entry;
                    }
                }
                else if (dir[j].DIR_Name[0] != 0xE5 && !include_deleted) {
                    if (strncmp((char*)dir[j].DIR_Name, filename, 11) == 0) {
                        if (sector_num) *sector_num = sector + i;
                        if (entry_index) *entry_index = j;
                        memcpy(&dir_entry, &dir[j], sizeof(DirEntry));
                        return &dir_entry;
                    }
                }
            }
        }

        uint32_t next_cluster = get_fat_entry(cluster);
        if (next_cluster >= EOC) break;
        cluster = next_cluster;
    }
    
    return NULL;
}

void delete_file(const char* filename) {
    if (!disk_img) {
        printf("Error: File system not open\n");
        return;
    }

    char expanded_name[12];
    convert_to_fat_filename(filename, expanded_name);

    uint32_t sector_num;
    int entry_index;
    DirEntry* entry = find_deleted_file_entry(expanded_name, current_dir_cluster, &sector_num, &entry_index, 0);

    if (!entry) {
        printf("Error: File not found\n");
        return;
    }

    if (entry->DIR_Attr & ATTRIBUTE_DIRECTORY) {
        printf("Error: Cannot delete a directory\n");
        return;
    }

    uint8_t buffer[SECTOR_SIZE];
    if (read_disk_sector(sector_num, buffer) != 1) {
        printf("Error: Could not read sector\n");
        return;
    }

    DirEntry* dir = (DirEntry*)buffer;
    dir[entry_index].DIR_Name[0] = 0xE5;

    if (write_disk_sector(sector_num, buffer) != 1) {
        printf("Error: Could not write sector\n");
        return;
    }

    printf("File deleted successfully\n");
}

void restore_deleted_file(const char* filename) {
    if (!disk_img) {
        printf("Error: File system not open\n");
        return;
    }

    char expanded_name[12];
    convert_to_fat_filename(filename, expanded_name);

    uint32_t sector_num;
    int entry_index;
    DirEntry* entry = find_deleted_file_entry(expanded_name, current_dir_cluster, &sector_num, &entry_index, 1);

    if (!entry) {
        printf("Error: Deleted file not found\n");
        return;
    }

    uint8_t buffer[SECTOR_SIZE];
    if (read_disk_sector(sector_num, buffer) != 1) {
        printf("Error: Could not read sector\n");
        return;
    }

    DirEntry* dir = (DirEntry*)buffer;
    dir[entry_index].DIR_Name[0] = expanded_name[0];

    if (write_disk_sector(sector_num, buffer) != 1) {
        printf("Error: Could not write sector\n");
        return;
    }

    printf("File restored successfully\n");
}

void read_file_content(const char *filename, uint32_t position, uint32_t num_bytes, int format)
{
    if (!disk_img)
    {
        printf("Error: File system not open\n");
        return;
    }

    char expanded_name[12];
    convert_to_fat_filename(filename, expanded_name);

    DirEntry *entry = find_file_entry(expanded_name, current_dir_cluster);
    if (!entry)
    {
        printf("Error: File not found\n");
        return;
    }

    if (entry->DIR_Attr & ATTRIBUTE_DIRECTORY)
    {
        printf("Error: Cannot read a directory\n");
        return;
    }

    if (position >= entry->DIR_FileSize)
    {
        printf("Error: Position outside file bounds\n");
        return;
    }

    if (position + num_bytes > entry->DIR_FileSize)
    {
        num_bytes = entry->DIR_FileSize - position;
    }

    uint32_t cluster = (entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
    uint32_t bytes_per_cluster = bs.bytesPerSector * bs.sectorsPerCluster;
    uint32_t clusters_to_skip = position / bytes_per_cluster;

    for (uint32_t i = 0; i < clusters_to_skip && cluster < EOC; i++)
    {
        cluster = get_fat_entry(cluster);
    }

    if (cluster >= EOC)
    {
        printf("Error: Invalid cluster chain\n");
        return;
    }

    uint8_t *buffer = malloc(num_bytes);
    if (!buffer)
    {
        printf("Error: Memory allocation failed\n");
        return;
    }

    uint32_t bytes_read = 0;
    uint32_t position_in_cluster = position % bytes_per_cluster;

    while (bytes_read < num_bytes && cluster < EOC)
    {
        uint32_t sector = get_first_sector_of_cluster(cluster);
        uint32_t sector_offset = position_in_cluster / bs.bytesPerSector;
        uint32_t byte_offset = position_in_cluster % bs.bytesPerSector;

        uint8_t sector_buffer[SECTOR_SIZE];
        read_disk_sector(sector + sector_offset, sector_buffer);

        uint32_t bytes_to_read = bs.bytesPerSector - byte_offset;
        if (bytes_to_read > (num_bytes - bytes_read))
        {
            bytes_to_read = num_bytes - bytes_read;
        }

        memcpy(buffer + bytes_read, sector_buffer + byte_offset, bytes_to_read);
        bytes_read += bytes_to_read;
        position_in_cluster = 0; 

        if (bytes_read < num_bytes)
        {
            cluster = get_fat_entry(cluster);
        }
    }

    for (uint32_t i = 0; i < bytes_read; i++)
    {
        switch (format)
        {
        case FORMAT_HEX:
            printf("0x%02X ", buffer[i]);
            break;
        case FORMAT_ASCII:
            printf("%c", buffer[i]);
            break;
        case FORMAT_DEC:
            printf("%d ", buffer[i]);
            break;
        }
    }
    printf("\n");

    free(buffer);
}

void upload_file(const char *filename, const char *newname) {
    if (!disk_img) {
        printf("Error: File system not open\n");
        return;
    }

    FILE *src_file = fopen(filename, "rb");
    if (!src_file) {
        printf("Error: File not found\n");
        return;
    }

    fseek(src_file, 0, SEEK_END);
    uint32_t file_size = ftell(src_file);
    fseek(src_file, 0, SEEK_SET);

    DirEntry new_entry;
    memset(&new_entry, 0, sizeof(DirEntry));
    const char *entry_name = newname ? newname : filename;

    char expanded_name[12];
    convert_to_fat_filename(entry_name, expanded_name);
    memcpy(new_entry.DIR_Name, expanded_name, 11);

    new_entry.DIR_Attr = ATTRIBUTE_ARCHIVE;
    new_entry.DIR_FileSize = file_size;

    uint32_t cluster = current_dir_cluster;
    uint32_t sector;
    uint8_t sector_buffer[SECTOR_SIZE];
    int entry_found = 0;
    int entry_index = 0;

    while (!entry_found) {
        sector = get_first_sector_of_cluster(cluster);
        
        for (int sec = 0; sec < bs.sectorsPerCluster; sec++) {
            if (read_disk_sector(sector + sec, sector_buffer) != 1) {
                printf("Error: Could not read directory sector\n");
                fclose(src_file);
                return;
            }

            DirEntry *dir = (DirEntry *)sector_buffer;
            for (int i = 0; i < bs.bytesPerSector / sizeof(DirEntry); i++) {
                if (dir[i].DIR_Name[0] == 0x00 || dir[i].DIR_Name[0] == 0xE5) {
                    entry_found = 1;
                    entry_index = i;
                    sector += sec;  
                    break;
                }
            }
            if (entry_found) break;
        }

        if (!entry_found) {
            uint32_t next_cluster = get_fat_entry(cluster);
            if (next_cluster >= EOC) {
                printf("Error: Directory full\n");
                fclose(src_file);
                return;
            }
            cluster = next_cluster;
        }
    }

    uint8_t buffer[SECTOR_SIZE];
    uint32_t bytes_remaining = file_size;
    uint32_t current_cluster = 0;
    uint32_t first_cluster = 0;

    while (bytes_remaining > 0) {
        size_t bytes_to_read = bytes_remaining < SECTOR_SIZE ? bytes_remaining : SECTOR_SIZE;
        if (fread(buffer, 1, bytes_to_read, src_file) != bytes_to_read) {
            printf("Error: Could not read source file\n");
            fclose(src_file);
            return;
        }

        if (current_cluster == 0) {
            uint32_t fat_size = bs.fatSize32 * bs.bytesPerSector;
            for (uint32_t i = 2; i < fat_size / 4; i++) {
                if (get_fat_entry(i) == 0) {
                    current_cluster = i;
                    if (first_cluster == 0) {
                        first_cluster = i;
                    }
                    break;
                }
            }
        }

        if (current_cluster == 0) {
            printf("Error: No free clusters available\n");
            fclose(src_file);
            return;
        }

        uint32_t data_sector = get_first_sector_of_cluster(current_cluster);
        if (write_disk_sector(data_sector, buffer) != 1) {
            printf("Error: Could not write to filesystem\n");
            fclose(src_file);
            return;
        }

        bytes_remaining -= bytes_to_read;

        if (bytes_remaining > 0) {
            uint32_t next_cluster = current_cluster + 1;
            update_fat_entry(current_cluster, bytes_remaining > 0 ? next_cluster : EOC);
            current_cluster = next_cluster;
        } 
        else {
            update_fat_entry(current_cluster, EOC);
        }
    }

    new_entry.DIR_FstClusLO = first_cluster & 0xFFFF;
    new_entry.DIR_FstClusHI = (first_cluster >> 16) & 0xFFFF;

    memcpy(&((DirEntry *)sector_buffer)[entry_index], &new_entry, sizeof(DirEntry));
    if (write_disk_sector(sector, sector_buffer) != 1) {
        printf("Error: Could not update directory entry\n");
    }

    fclose(src_file);
    printf("File copied successfully\n");
}

void cmd_stat(const char *filename)
{
    if (!disk_img)
    {
        printf("Error: File system not open\n");
        return;
    }

    char expanded_name[12];
    convert_to_fat_filename(filename, expanded_name);

    DirEntry *entry = find_file_entry(expanded_name, current_dir_cluster);
    if (!entry)
    {
        printf("Error: File not found\n");
        return;
    }

    printf("Attributes: ");
    if (entry->DIR_Attr & ATTRIBUTE_READ_ONLY)
        printf("READ_ONLY ");
    if (entry->DIR_Attr & ATTRIBUTE_HIDDEN)
        printf("HIDDEN ");
    if (entry->DIR_Attr & ATTRIBUTE_SYSTEM)
        printf("SYSTEM ");
    if (entry->DIR_Attr & ATTRIBUTE_DIRECTORY)
        printf("DIRECTORY ");
    if (entry->DIR_Attr & ATTRIBUTE_ARCHIVE)
        printf("ARCHIVE ");
    printf("\n");

    uint32_t first_cluster = (entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
    printf("Starting Cluster: %u\n", first_cluster);
    if (!(entry->DIR_Attr & ATTRIBUTE_DIRECTORY))
    {
        printf("File Size: %u bytes\n", entry->DIR_FileSize);
    }
    else
    {
        printf("File Size: 0 bytes\n");
    }
}

void cmd_get(const char *filename, const char *newname)
{
    if (!disk_img)
    {
        printf("Error: File system not open\n");
        return;
    }

    char expanded_name[12];
    convert_to_fat_filename(filename, expanded_name);

    DirEntry *entry = find_file_entry(expanded_name, current_dir_cluster);
    if (!entry)
    {
        printf("Error: File not found\n");
        return;
    }

    if (entry->DIR_Attr & ATTRIBUTE_DIRECTORY)
    {
        printf("Error: Cannot get a directory\n");
        return;
    }

    const char *output_name = newname ? newname : filename;
    FILE *outfile = fopen(output_name, "wb");
    if (!outfile)
    {
        printf("Error: Cannot create output file\n");
        return;
    }

    uint32_t cluster = (entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
    uint32_t size_remaining = entry->DIR_FileSize;
    uint8_t buffer[SECTOR_SIZE];

    while (size_remaining > 0 && cluster < EOC)
    {
        uint32_t sector = get_first_sector_of_cluster(cluster);

        for (int i = 0; i < bs.sectorsPerCluster && size_remaining > 0; i++)
        {
            read_disk_sector(sector + i, buffer);
            uint32_t bytes_to_write = size_remaining < bs.bytesPerSector ? size_remaining : bs.bytesPerSector;
            fwrite(buffer, 1, bytes_to_write, outfile);
            size_remaining -= bytes_to_write;
        }

        cluster = get_fat_entry(cluster);
    }

    fclose(outfile);
}

void cmd_cd(const char *dirname)
{
    if (!disk_img)
    {
        printf("Error: File system not open\n");
        return;
    }

    if (strcmp(dirname, "..") == 0)
    {
        if (current_dir_cluster != bs.rootCluster)
        {
            //TODO:implement getting parent diractory
        }
        return;
    }

    char expanded_name[12];
    convert_to_fat_filename(dirname, expanded_name);

    DirEntry *entry = find_file_entry(expanded_name, current_dir_cluster);
    if (!entry)
    {
        printf("Error: Directory not found\n");
        return;
    }

    if (!(entry->DIR_Attr & ATTRIBUTE_DIRECTORY))
    {
        printf("Error: Not a directory\n");
        return;
    }

    current_dir_cluster = (entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
}

void cmd_ls(void)
{
    if (!disk_img)
    {
        printf("Error: File system not open\n");
        return;
    }
    list_directory_entries(current_dir_cluster);
}

void list_directory_entries(uint32_t cluster) {
    uint32_t sector = get_first_sector_of_cluster(cluster);
    uint32_t bytes_per_sector = bs.bytesPerSector;
    uint32_t bytes_per_cluster = bytes_per_sector * bs.sectorsPerCluster;
    uint8_t* cluster_buffer = malloc(bytes_per_cluster);

    if (!cluster_buffer) {
        printf("Error: Memory allocation failed\n");
        return;
    }

    while (1) {
        fseek(disk_img, sector * bytes_per_sector, SEEK_SET);
        if (fread(cluster_buffer, bytes_per_cluster, 1, disk_img) != 1) {
            printf("Error: Could not read cluster\n");
            free(cluster_buffer);
            return;
        }

        for (uint32_t i = 0; i < bytes_per_cluster; i += sizeof(DirEntry)) {
            DirEntry* dir = (DirEntry*)(cluster_buffer + i);

            if (dir->DIR_Name[0] == 0x00) {
                free(cluster_buffer);
                return; 
            }

            if (dir->DIR_Name[0] == 0xE5 ||
                (dir->DIR_Attr & ATTRIBUTE_VOLUME_ID) ||
                dir->DIR_Name[0] == '.' ||
                (unsigned char)dir->DIR_Name[0] == 0xE5) 
            {
                continue;
            }

            char name[13];
            memset(name, 0, sizeof(name));

            memcpy(name, dir->DIR_Name, 8);
            if (dir->DIR_Name[8] != ' ') {
                int nameLen = strlen(name);
                while (nameLen > 0 && name[nameLen - 1] == ' ')
                    nameLen--;
                name[nameLen] = '.';
                memcpy(name + nameLen + 1, dir->DIR_Name + 8, 3);
            }

            int len = strlen(name);
            while (len > 0 && (name[len - 1] == ' ' || (name[len - 1] == '.' && len > 1)))
                name[--len] = 0;

            if (len > 0) {
                printf("%s", name);
                if (dir->DIR_Attr & ATTRIBUTE_DIRECTORY)
                    printf("/");
                printf("\n");
            }
        }

        uint32_t next_cluster = get_fat_entry(cluster);
        if (next_cluster >= EOC)
            break;

        cluster = next_cluster;
        sector = get_first_sector_of_cluster(cluster);
    }

    free(cluster_buffer);
}

int open_filesystem(const char *filename)
{
    if (strlen(filename) > 100)
    {
        printf("Error: Filename too long\n");
        return -1;
    }

    disk_img = fopen(filename, "rb+");
    if (!disk_img)
    {
        return -1;
    }

    fseek(disk_img, 0, SEEK_SET);
    if (fread(&bs, sizeof(BootSector), 1, disk_img) != 1)
    {
        fclose(disk_img);
        disk_img = NULL;
        return -1;
    }

    strncpy(current_image_name, filename, Mx_FILENAME_LENGTH - 1);
    current_image_name[Mx_FILENAME_LENGTH - 1] = '\0';
    current_dir_cluster = bs.rootCluster;

    return 0;
}

void close_filesystem(void)
{
    if (disk_img)
    {
        fclose(disk_img);
        disk_img = NULL;
        current_image_name[0] = '\0';
    }
}

void display_filesystem_info(void)
{
    printf("bytesPerSector: 0x%X (%d)\n", bs.bytesPerSector, bs.bytesPerSector);
    printf("sectorsPerCluster: 0x%X (%d)\n", bs.sectorsPerCluster, bs.sectorsPerCluster);
    printf("reservedSectorCount: 0x%X (%d)\n", bs.reservedSectorCount, bs.reservedSectorCount);
    printf("numberOfFATs: 0x%X (%d)\n", bs.numberOfFATs, bs.numberOfFATs);
    printf("fatSize32: 0x%X (%d)\n", bs.fatSize32, bs.fatSize32);
    printf("extendedFlags: 0x%X (%d)\n", bs.extendedFlags, bs.extendedFlags);
    printf("rootCluster: 0x%X (%d)\n", bs.rootCluster, bs.rootCluster);
    printf("fsInfoSector: 0x%X (%d)\n", bs.fsInfoSector, bs.fsInfoSector);
}
// add execute_command
int main()
{
    char cmd_line[Mx_COMMAND_LENGTH];

    while (1)
    {
        printf("mfs> ");
        if (!fgets(cmd_line, sizeof(cmd_line), stdin))
            break;

        if (cmd_line[0] == '\n')
            continue;

        cmd_line[strcspn(cmd_line, "\n")] = 0;

        execute_command(cmd_line);
    }

    if (disk_img)
        close_filesystem();
    return 0;
}