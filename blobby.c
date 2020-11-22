// blobby.c
// blob file archiver
// COMP1521 20T3 Assignment 2
// Written by Benji Schwartz(z5316730)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <spawn.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

// the first byte of every blobette has this value
#define BLOBETTE_MAGIC_NUMBER          0x42

// number of bytes in fixed-length blobette fields
#define BLOBETTE_MAGIC_NUMBER_BYTES    1
#define BLOBETTE_MODE_LENGTH_BYTES     3
#define BLOBETTE_PATHNAME_LENGTH_BYTES 2
#define BLOBETTE_CONTENT_LENGTH_BYTES  6
#define BLOBETTE_HASH_BYTES            1

// maximum number of bytes in variable-length blobette fields
#define BLOBETTE_MAX_PATHNAME_LENGTH   65535
#define BLOBETTE_MAX_CONTENT_LENGTH    281474976710655

typedef enum action {
    a_invalid,
    a_list,
    a_extract,
    a_create
} action_t;


void usage(char *myname);
action_t process_arguments(int argc, char *argv[], char **blob_pathname,
                           char ***pathnames, int *compress_blob);

void list_blob(char *blob_pathname);
void extract_blob(char *blob_pathname);
void create_blob(char *blob_pathname, char *pathnames[], int compress_blob);
uint8_t blobby_hash(uint8_t hash, uint8_t byte);

// Additional prototypes
void add_folder_to_blob(char *new_blob, char *directory);
void add_file_to_blob(char *blob, char *file_pathname);
void unpack_pathnames(char *new_blob, char *file_pathname);

int main(int argc, char *argv[]) {
    char *blob_pathname = NULL;
    char **pathnames = NULL;
    int compress_blob = 0;

    action_t action = process_arguments(argc, argv, &blob_pathname, &pathnames,
                                        &compress_blob);

    switch (action) {
    case a_list:
        list_blob(blob_pathname);
        break;

    case a_extract:
        extract_blob(blob_pathname);
        break;

    case a_create:
        create_blob(blob_pathname, pathnames, compress_blob);
        break;

    default:
        usage(argv[0]);
    }

    return 0;
}

// print a usage message and exit

void usage(char *myname) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "\t%s -l <blob-file>\n", myname);
    fprintf(stderr, "\t%s -x <blob-file>\n", myname);
    fprintf(stderr, "\t%s [-z] -c <blob-file> pathnames [...]\n", myname);
    exit(1);
}

// process command-line arguments
// check we have a valid set of arguments
// and return appropriate action
// **blob_pathname set to pathname for blobfile
// ***pathname set to a list of pathnames for the create action
// *compress_blob set to an integer for create action

action_t process_arguments(int argc, char *argv[], char **blob_pathname,
                           char ***pathnames, int *compress_blob) {
    extern char *optarg;
    extern int optind, optopt;
    int create_blob_flag = 0;
    int extract_blob_flag = 0;
    int list_blob_flag = 0;
    int opt;
    while ((opt = getopt(argc, argv, ":l:c:x:z")) != -1) {
        switch (opt) {
        case 'c':
            create_blob_flag++;
            *blob_pathname = optarg;
            break;

        case 'x':
            extract_blob_flag++;
            *blob_pathname = optarg;
            break;

        case 'l':
            list_blob_flag++;
            *blob_pathname = optarg;
            break;

        case 'z':
            (*compress_blob)++;
            break;

        default:
            return a_invalid;
        }
    }

    if (create_blob_flag + extract_blob_flag + list_blob_flag != 1) {
        return a_invalid;
    }

    if (list_blob_flag && argv[optind] == NULL) {
        return a_list;
    } else if (extract_blob_flag && argv[optind] == NULL) {
        return a_extract;
    } else if (create_blob_flag && argv[optind] != NULL) {
        *pathnames = &argv[optind];
        return a_create;
    }

    return a_invalid;
}


// list the contents of blob_pathname

void list_blob(char *blob_pathname) {
    // prints out:
    //      - file/directory permissions in octal
    //      - file/directory size in bytes
    //      - file/directory pathname

    FILE* f = fopen(blob_pathname, "r"); // read a file with the given name
    if (f == NULL) {
        perror("Something went wrong");
    }

    int ch = 0;
    int hash = 0;
    while ((ch = fgetc(f))!= EOF) {
        hash = blobby_hash(00, ch);
        if (ch != BLOBETTE_MAGIC_NUMBER) {
            fprintf(stderr, "ERROR: Magic byte of blobette incorrect\n");
            break;
        }

        // read mode
        unsigned long mode = 0;
        for (int i = 0; i < BLOBETTE_MODE_LENGTH_BYTES; i++) {
            mode <<= 8;
            ch = fgetc(f);
            mode |= ch;
            hash = blobby_hash(hash, ch);
        }

        // read p length
        unsigned long p_length = 0;
        for (int i = 0; i < BLOBETTE_PATHNAME_LENGTH_BYTES; i++) {
            p_length <<= 8;
            ch = fgetc(f);
            p_length |= ch;
            hash = blobby_hash(hash, ch);
        }

        // read c length
        unsigned long c_length = 0;
        for (int i = 0; i < BLOBETTE_CONTENT_LENGTH_BYTES; i++) {
            c_length <<= 8;
            ch = fgetc(f);
            c_length |= ch;
            hash = blobby_hash(hash, ch);
        }

        char pathname[p_length + 1];
        for (int i = 0; i < p_length; i++){
            ch = fgetc(f);
            pathname[i] = ch;
            hash = blobby_hash(hash, ch);
        }
        pathname[p_length] = '\0';

        for (int i = 0; i < c_length; i++){
            ch = fgetc(f);
            hash = blobby_hash(hash, ch);
        }

        // read hash
        ch = fgetc(f);

        // start over
        printf("%06lo %5lu %s\n", mode, c_length, pathname);
    }
}


// extract the contents of blob_pathname

void extract_blob(char *blob_pathname) {
    FILE* comp_f = fopen(blob_pathname, "r");
    uint64_t detector = 0;
    for (int i = 0; i < 6; i++) {
        detector |= fgetc(comp_f);
    } if (detector == 0xFD377A585A00) {
        printf("compressed file\n");
    } fclose(comp_f);

    FILE* f = fopen(blob_pathname, "r"); // read a file with the given name
    if (f == NULL) {
        perror("Something went wrong");
    }

    int hash_correct = 1;
    int ch = 0;
    int hash = 0;
    while ((ch = fgetc(f))!= EOF) {
        hash = blobby_hash(00, ch);
        if (ch != BLOBETTE_MAGIC_NUMBER) {
            fprintf(stderr, "ERROR: Magic byte of blobette incorrect\n");
            exit(1);
        }

        // read mode
        unsigned long mode = 0;
        for (int i = 0; i < BLOBETTE_MODE_LENGTH_BYTES; i++) {
            mode <<= 8;
            ch = fgetc(f);
            mode |= ch;
            hash = blobby_hash(hash, ch);
        }

        // read p length
        unsigned long p_length = 0;
        for (int i = 0; i < BLOBETTE_PATHNAME_LENGTH_BYTES; i++) {
            p_length <<= 8;
            ch = fgetc(f);
            p_length |= ch;
            hash = blobby_hash(hash, ch);
        }

        // read c length
        unsigned long c_length = 0;
        for (int i = 0; i < BLOBETTE_CONTENT_LENGTH_BYTES; i++) {
            c_length <<= 8;
            ch = fgetc(f);
            c_length |= ch;
            hash = blobby_hash(hash, ch);
        }

        char pathname[p_length + 1];
        for (int i = 0; i < p_length; i++){
            ch = fgetc(f);
            pathname[i] = ch;
            hash = blobby_hash(hash, ch);
        }
        pathname[p_length] = '\0';

        if (c_length == 0) {    // dealing with a directory
            printf("Creating directory: %s\n", pathname);
            // extract_directory(pathname);
            if (mkdir(pathname, 0755) != 0) {
                perror(pathname);
                exit(1);
            }
            if (chmod(pathname, mode) != 0) {
                perror("unable to update mode of directory");
            }

            // read hash
            ch = fgetc(f);

            if (hash != ch) {
                hash_correct = -1;
            }

            if (hash_correct != 1) {
                fprintf(stderr, "ERROR: blob hash incorrect\n");
            }
        } 
        
        else {                  // dealing with a file
            printf("Extracting: %s\n", pathname);

            FILE* new_f = fopen(pathname, "w");

            // set mode of new file with chmod()
            if (chmod(pathname, mode) != 0) {
                perror("unable to update mode of file");
            }

            // write contents of new file
            for (int i = 0; i < c_length; i++){
                ch = fgetc(f);
                fputc(ch, new_f);
                hash = blobby_hash(hash, ch);
            }

            // read hash
            ch = fgetc(f);

            if (hash != ch) {
                hash_correct = -1;
            }

            if (hash_correct != 1) {
                fprintf(stderr, "ERROR: blob hash incorrect\n");
            }
        }
    }
}

// create blob_pathname from NULL-terminated array pathnames
// compress with xz if compress_blob non-zero (subset 4)

/*
    examine each pathname
    if a FILE is specified:
            --> add all the preceding directories
            --> examples/2_files/hello.txt     --> [examples, examples/2_files, examples/2_files/hello.txt]
    if a DIRECTORY is specified:
            --> recursively add all the contents of that directory
            --> then treat as FILE and add all the preceding dictionaries

    unpack the given pathname and add these paths to the blob
*/
void create_blob(char *blob_pathname, char *pathnames[], int compress_blob) {

    // if (compress_blob != 0) {
    //     // Create a list of file actions to be carried out on spawned process.
    //     posix_spawn_file_actions_t actions;
    //     if (posix_spawn_file_actions_init(&actions) != 0) {
    //         perror("posix_spawn_file_actions_init");
    //         return;
    //     }

    //     // Spawn a process running xz to compress this file. 
    //     char *sort_argv[] = {"xz", "-z", blob_pathname};
    //     pid_t pid;
    //     extern char **environ;
    //     if (posix_spawn(&pid, "/usr/bin/xz", &actions, NULL, sort_argv, environ) != 0) {
    //         perror("spawn");
    //         return;
    //     }

    //     wait(pid);

    //     return;
    // }

    FILE* new_blob = fopen(blob_pathname, "w"); // create blob
    fclose(new_blob);

    for (int p = 0; pathnames[p]; p++) {

        struct stat file_info;
        stat(pathnames[p], &file_info);
        if (S_ISDIR(file_info.st_mode)) {
            unpack_pathnames(blob_pathname, pathnames[p]);
            add_folder_to_blob(blob_pathname, pathnames[p]);
        } else {
            unpack_pathnames(blob_pathname, pathnames[p]);
        }

    }
}


// add all files to the specified blob directory
// ADAPTED FROM: https://codeforwin.org/2018/03/c-program-to-list-all-files-in-a-directory-recursively.html 
void add_folder_to_blob(char *new_blob, char *directory) {
    char path[BLOBETTE_MAX_PATHNAME_LENGTH];
    struct dirent *dp;
    DIR *dir = opendir(directory);   
    if (!dir)
        return;

    while ((dp = readdir(dir)) != NULL)
    {
        if ( strcmp(dp->d_name, "..") != 0 && strcmp(dp->d_name, ".") != 0)
        {
            strcpy(path, directory);
            strcat(path, "/");
            strcat(path, dp->d_name);

            // include levels of directory above path
            add_file_to_blob(new_blob, path);
            // recursively call add_folder
            add_folder_to_blob(new_blob, path);
        }
    }
    closedir(dir);
}

// include all levels of directories above a given file
void add_file_to_blob(char *blob, char *file_pathname) {

    // open the input blob and write to the END of blob
    FILE* new_blob = fopen(blob, "a");

    int hash = 0;

    printf("Adding: %s\n", file_pathname);
    hash = blobby_hash(hash, BLOBETTE_MAGIC_NUMBER);
    fputc(BLOBETTE_MAGIC_NUMBER, new_blob);

    struct stat buf;
    if (stat(file_pathname, &buf) != 0) {
        fprintf(stderr, "Failed to load stat from file\n");
        exit(EXIT_FAILURE);
    }

    // extract mode from pathname[p] and use bit-shift to store in blob
    // concatenate to fit field length of 3 bytes
    mode_t mode = buf.st_mode;
    for (int i = 0; i < BLOBETTE_MODE_LENGTH_BYTES; i++) {
        uint32_t mask = 0xFF;
        mask <<= 8*(2 - i);
        mask &= mode;
        mask >>= 8*(2 - i);
        fputc(mask, new_blob);
        hash = blobby_hash(hash, mask);
    }

    // store pathname length - concatenate to fit field length of 2 bytes
    int pathname_length = strlen(file_pathname);
    for (int i = 0; i < BLOBETTE_PATHNAME_LENGTH_BYTES; i++) {
        uint32_t mask = 0xFF;
        mask <<= 8*(1 - i);
        mask &= pathname_length;
        mask >>= 8*(1 - i);
        fputc(mask, new_blob);
        hash = blobby_hash(hash, mask);
    }

    // store content length - concatenate to fit field length of 6 bytes
    off_t content_length = buf.st_size;

    if (S_ISDIR(mode)) {
        content_length = 0;
    }

    for (int i = 0; i < BLOBETTE_CONTENT_LENGTH_BYTES; i++) {
        uint64_t mask = 0xFF;
        mask <<= 8*(5 - i);
        mask &= content_length;
        mask >>= 8*(5 - i);
        fputc(mask, new_blob);
        hash = blobby_hash(hash, mask);
    }

    // store pathname
    for (int i = 0; i < pathname_length; i++) {
        fputc(file_pathname[i], new_blob);
        hash = blobby_hash(hash, file_pathname[i]);
    }

    if (!(S_ISDIR(mode))) {
        FILE* f = fopen(file_pathname, "r");
        int ch = 0;
        while((ch = fgetc(f)) != EOF) {
            fputc(ch, new_blob);
            hash = blobby_hash(hash, ch);
        }
    }
    // store blobette hash
    fputc(hash, new_blob);
    fclose(new_blob);
}


// helper function to unpack previous directories of a given file
// e.g.     unpack_pathnames(examples/2_files.d/hello.txt) --> [examples, examples/2_files.d, examples/2_files.d/hello.txt]
void unpack_pathnames(char *new_blob, char *file_pathname) {    
    char ch;
    int i = 0;
    char temp_array[FILENAME_MAX];
    while ((ch = file_pathname[i]) != '\0') {
        // printf("at letter %c now\n", ch);
        if (ch == '/') {
            temp_array[i] = '\0';
            add_file_to_blob(new_blob, temp_array);
        }
        temp_array[i] = ch;
        i++;
    }
    temp_array[i] = '\0';
    add_file_to_blob(new_blob, temp_array);
}

// Lookup table for a simple Pearson hash
// https://en.wikipedia.org/wiki/Pearson_hashing
// This table contains an arbitrary permutation of integers 0..255

const uint8_t blobby_hash_table[256] = {
    241, 18,  181, 164, 92,  237, 100, 216, 183, 107, 2,   12,  43,  246, 90,
    143, 251, 49,  228, 134, 215, 20,  193, 172, 140, 227, 148, 118, 57,  72,
    119, 174, 78,  14,  97,  3,   208, 252, 11,  195, 31,  28,  121, 206, 149,
    23,  83,  154, 223, 109, 89,  10,  178, 243, 42,  194, 221, 131, 212, 94,
    205, 240, 161, 7,   62,  214, 222, 219, 1,   84,  95,  58,  103, 60,  33,
    111, 188, 218, 186, 166, 146, 189, 201, 155, 68,  145, 44,  163, 69,  196,
    115, 231, 61,  157, 165, 213, 139, 112, 173, 191, 142, 88,  106, 250, 8,
    127, 26,  126, 0,   96,  52,  182, 113, 38,  242, 48,  204, 160, 15,  54,
    158, 192, 81,  125, 245, 239, 101, 17,  136, 110, 24,  53,  132, 117, 102,
    153, 226, 4,   203, 199, 16,  249, 211, 167, 55,  255, 254, 116, 122, 13,
    236, 93,  144, 86,  59,  76,  150, 162, 207, 77,  176, 32,  124, 171, 29,
    45,  30,  67,  184, 51,  22,  105, 170, 253, 180, 187, 130, 156, 98,  159,
    220, 40,  133, 135, 114, 147, 75,  73,  210, 21,  129, 39,  138, 91,  41,
    235, 47,  185, 9,   82,  64,  87,  244, 50,  74,  233, 175, 247, 120, 6,
    169, 85,  66,  104, 80,  71,  230, 152, 225, 34,  248, 198, 63,  168, 179,
    141, 137, 5,   19,  79,  232, 128, 202, 46,  70,  37,  209, 217, 123, 27,
    177, 25,  56,  65,  229, 36,  197, 234, 108, 35,  151, 238, 200, 224, 99,
    190
};

// Given the current hash value and a byte
// blobby_hash returns the new hash value
//
// Call repeatedly to hash a sequence of bytes, e.g.:
// uint8_t hash = 0;
// hash = blobby_hash(hash, byte0);
// hash = blobby_hash(hash, byte1);
// hash = blobby_hash(hash, byte2);
// ...

uint8_t blobby_hash(uint8_t hash, uint8_t byte) {
    return blobby_hash_table[hash ^ byte];
}
