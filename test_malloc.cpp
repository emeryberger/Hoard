#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

int main(int argc, char* argv[]) {
    printf("=== Hoard Interposition Test ===\n");
    printf("Starting memory allocation test...\n");
    fflush(stdout);

    // Test malloc/free
    printf("Testing malloc/free...\n");
    fflush(stdout);
    void* ptr1 = malloc(100);
    printf("  malloc(100) = %p\n", ptr1);
    fflush(stdout);
    free(ptr1);
    printf("  free() completed\n");
    fflush(stdout);

    // Test calloc
    printf("Testing calloc...\n");
    fflush(stdout);
    void* ptr2 = calloc(10, 20);
    printf("  calloc(10, 20) = %p\n", ptr2);
    fflush(stdout);
    free(ptr2);
    printf("  free() completed\n");
    fflush(stdout);

    // Test realloc
    printf("Testing realloc...\n");
    fflush(stdout);
    void* ptr3 = malloc(50);
    printf("  malloc(50) = %p\n", ptr3);
    fflush(stdout);
    ptr3 = realloc(ptr3, 200);
    printf("  realloc(200) = %p\n", ptr3);
    fflush(stdout);
    free(ptr3);
    printf("  free() completed\n");
    fflush(stdout);

    // Test new/delete
    printf("Testing new/delete...\n");
    fflush(stdout);
    int* ptr4 = new int(42);
    printf("  new int(42) = %p, value = %d\n", ptr4, *ptr4);
    fflush(stdout);
    delete ptr4;
    printf("  delete completed\n");
    fflush(stdout);

    // Test new[]/delete[]
    printf("Testing new[]/delete[]...\n");
    fflush(stdout);
    int* ptr5 = new int[10];
    printf("  new int[10] = %p\n", ptr5);
    fflush(stdout);
    delete[] ptr5;
    printf("  delete[] completed\n");
    fflush(stdout);

    printf("\n=== All tests completed successfully ===\n");
    printf("If you see 'Hoard: Memory allocator active' above, interposition is working!\n");
    fflush(stdout);

    return 0;
}
