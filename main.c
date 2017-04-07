/*
Main program for the virtual memory project.
Make all of your modifications to this file.
You may add or rearrange any code or data as you need.
The header files page_table.h and disk.h explain
how to use the page table and disk interfaces.
*/

#include "page_table.h"
#include "disk.h"
#include "program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define NOT_FOUND -1

int search(int start, int end, int k);

struct disk *disk;      // disk
int numPageFaults = 0;  // Number of page faults
int numDiskWrite = 0;   // Number of disk writes
int numDiskRead = 0;    // Number of disk reads
int runMode;            // RAND, FIFO, or CUSTOM
int *ptArr;             // Page Table array
int *scArr;
int count = 0;          // counter


void page_fault_handler( struct page_table *pt, int page )
{
    int numPages = page_table_get_npages(pt);       // Number of pages
    int numFrames = page_table_get_nframes(pt);     // Number of frames
    char *pmem = page_table_get_physmem(pt);        // Physical memory pointer
    //char *scbits = page_table_get_virtsmem(pt);

    if (numFrames >= numPages) {    // Getting started
        printf("Page fault on page #%d\n", page);
        
        page_table_set_entry(pt, page, page, PROT_READ|PROT_WRITE);
        numPageFaults++;

        numDiskWrite = 0;
        numDiskRead = 0;

    } else if (runMode == 1) {      // RAND
        numPageFaults++;

        int val = search(0, numFrames - 1, page);   // search page table array for page
        int tmp = lrand48() % numFrames;

        if (val > NOT_FOUND) {              // Page was found, set entry and continue
            page_table_set_entry(pt, page, val, PROT_READ|PROT_WRITE);
            numPageFaults--;
        } else if (count < numFrames) { // haven't been through all of the frames

            // find next page until a page already accessed is found
            while (ptArr[tmp] != NOT_FOUND) {
                tmp = lrand48() % numFrames;
                numPageFaults++;
            }

            // set entry and read from disk
            page_table_set_entry(pt, page, tmp, PROT_READ);
            disk_read(disk, page, &pmem[tmp * PAGE_SIZE]);

            // increment tracking variables
            numDiskRead++;
            count++;
            ptArr[tmp] = page;
        } else { // found all frames, read and write from virtmem
            disk_write(disk, ptArr[tmp], &pmem[tmp * PAGE_SIZE]);
            disk_read(disk, page, &pmem[tmp * PAGE_SIZE]);

            numDiskWrite++;
            numDiskRead++;

            page_table_set_entry(pt, page, tmp, PROT_READ);
            ptArr[tmp] = page;
        }

    } else if (runMode == 2) {          // FIFO
        numPageFaults++;
        int val = search(0, numFrames - 1, page); // search ptArr for page

        if (val > NOT_FOUND) {      // page in ptArr

            page_table_set_entry(pt, page, val, PROT_READ|PROT_WRITE);
            count--;
            numPageFaults--;

        } else if (ptArr[count] == NOT_FOUND) {

            // Page[count] has not been accessed
            // set entry at count and read from count * PAGE_SIZE

            page_table_set_entry(pt, page, count, PROT_READ);
            disk_read(disk, page, &pmem[count * PAGE_SIZE]);
            numDiskRead++;

        } else {
            // Page fault, write and read from pmem
            disk_write(disk, ptArr[count], &pmem[count * PAGE_SIZE]);
            disk_read(disk, page, &pmem[count * PAGE_SIZE]);
            numDiskWrite++;
            numDiskRead++;
            page_table_set_entry(pt, page, count, PROT_READ);

        }

        ptArr[count] = page;
        count = (count + 1) % numFrames;
 

    } else if (runMode == 3) {          // CUSTOM - second chance fifo
        numPageFaults++;
        int tmp = page % numFrames;
        int val = search(0, numFrames - 1, page);

        // Page has not been accessed on this round or the last

        if (ptArr[tmp] == page || val > NOT_FOUND) {

            page_table_set_entry(pt, page, tmp, PROT_READ|PROT_WRITE);
            numPageFaults--;

        } else if (ptArr[tmp] == NOT_FOUND) {  // page has not been read

            page_table_set_entry(pt, page, tmp, PROT_READ);
            disk_read(disk, page, &pmem[tmp * PAGE_SIZE]);
            numDiskRead++;

        } else {

            disk_write(disk, ptArr[tmp], &pmem[tmp * PAGE_SIZE]);
            disk_read(disk, page, &pmem[tmp * PAGE_SIZE]);
            numDiskWrite++;
            numDiskRead++;
            page_table_set_entry(pt, page, tmp, PROT_READ);
        }

        ptArr[tmp] = page;

    } else {
        printf("page fault on page #%d\n",page);
        exit(1);
    }

}

int search(int start, int end, int k) {         // simple linear search function
    int i;
    for (i = start; i <= end; i++) {
        if (ptArr[i] == k)
            return i;
    }
    return NOT_FOUND;
}

int main( int argc, char *argv[] )
{
    if(argc!=5) {
        printf("use: virtmem <npages> <nframes> <rand|fifo|lru|custom> <sort|scan|focus>\n");
        return 1;
    }

    int npages = atoi(argv[1]);
    int nframes = atoi(argv[2]);
    const char *program = argv[4];

    // Parse run mode
    if ((strcmp(argv[3], "rand")) == 0)
        runMode = 1;
    else if ((strcmp(argv[3], "fifo")) == 0)
        runMode = 2;
    else
        runMode = 3;

    // Initialize page table array
    ptArr = (int *) malloc(nframes * sizeof(int));
    int loop;
    for (loop = 0; loop < nframes; loop++)      // populate with -1
        ptArr[loop] = -1;

    scArr = (int *) malloc(nframes * sizeof(int));
    for (loop = 0; loop < nframes; loop++)      // populate with -1
        scArr[loop] = -1;

    disk = disk_open("myvirtualdisk",npages);
    if(!disk) {
        fprintf(stderr,"couldn't create virtual disk: %s\n",strerror(errno));
        return 1;
    }


    struct page_table *pt = page_table_create( npages, nframes, page_fault_handler );
    if(!pt) {
        fprintf(stderr,"couldn't create page table: %s\n",strerror(errno));
        return 1;
    }

    char *virtmem = page_table_get_virtmem(pt);

    //char *physmem = page_table_get_physmem(pt);

    if(!strcmp(program,"sort")) {
        sort_program(virtmem,npages*PAGE_SIZE);

    } else if(!strcmp(program,"scan")) {
        scan_program(virtmem,npages*PAGE_SIZE);

    } else if(!strcmp(program,"focus")) {
        focus_program(virtmem,npages*PAGE_SIZE);

    } else {
        fprintf(stderr,"unknown program: %s\n",argv[3]);
        return 1;
    }
    // Print stats about program run
    printf("Page Faults: %d, Disk Writes: %d, Disk Reads: %d\n", numPageFaults, numDiskWrite, numDiskRead);

    page_table_delete(pt);
    disk_close(disk);

    return 0;
}