/*
 * Copyright 2018 Chris Seaton
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <setjmp.h>
#include <time.h>
#include <sys/mman.h>

static bool interrupt_flag = false;
void *poll_page;
static jmp_buf interrupt_handler;

static void uninterruptible_matrix_multiply(int size, double *a, double *b, double *c) {
  for (int i = 0; i < size; i++) {
    for (int j = 0; j < size; j++) {
      double sum = 0;
      for (int k = 0; k < size; k++) {
        sum += a[i*size + k] * b[k*size + j];        
      }
      c[i*size + j] = sum;
    }
  }
}

static void flag_matrix_multiply(int size, double *a, double *b, double *c) {
  for (int i = 0; i < size; i++) {
    for (int j = 0; j < size; j++) {
      double sum = 0;
      for (int k = 0; k < size; k++) {
        if (interrupt_flag) {
          longjmp(interrupt_handler, 1);
        }
        sum += a[i*size + k] * b[k*size + j];        
      }
      c[i*size + j] = sum;
    }
  }
}

static void write_matrix_multiply(int size, double *a, double *b, double *c) {
  for (int i = 0; i < size; i++) {
    for (int j = 0; j < size; j++) {
      double sum = 0;
      for (int k = 0; k < size; k++) {
        *((int *)poll_page) = 14;
        sum += a[i*size + k] * b[k*size + j];        
      }
      c[i*size + j] = sum;
    }
  }
}

static void test_matrix_multiply(int size, double *a, double *b, double *c) {
  for (int i = 0; i < size; i++) {
    for (int j = 0; j < size; j++) {
      double sum = 0;
      for (int k = 0; k < size; k++) {
        __asm__("testq %rax, _poll_page(%rip)");
        sum += a[i*size + k] * b[k*size + j];        
      }
      c[i*size + j] = sum;
    }
  }
}

static void setup_poll_page() {
  poll_page = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (poll_page == MAP_FAILED) {
    fprintf(stderr, "error creating poll page: %s\n", strerror(errno));
    abort();
  }
  fprintf(stderr, "poll page installed to %p\n", poll_page);
}

static void invalidate_poll_page() {
  fprintf(stderr, "invalidating poll page\n");
  mprotect(poll_page, sizeof(int), PROT_NONE);
}

static void protection_handler(int signum, siginfo_t *siginfo, void *context) {
  fprintf(stderr, "protection fault handled\n");
  mprotect(poll_page, sizeof(int), PROT_READ | PROT_WRITE);
  longjmp(interrupt_handler, 1);
}

static void setup_protection_handler() {
  struct sigaction action;
  action.sa_flags = SA_SIGINFO;
  action.sa_sigaction = protection_handler;
  if (sigaction(SIGBUS, &action, NULL) != 0) {
    fprintf(stderr, "error setting up bus error handler: %s\n", strerror(errno));
    abort();
  }
}

static void print_elapsed_time(struct timespec *start) {
  struct timespec finish;
  clock_gettime(CLOCK_MONOTONIC_RAW, &finish);
  fprintf(stderr, "time: %fs\n", (double) (finish.tv_sec - start->tv_sec + (finish.tv_nsec - start->tv_nsec) / 1e9));
}

int main() {
  setup_poll_page();
  setup_protection_handler();
  
  int size = 1024;
  
  double *a = malloc(sizeof(double) * size * size);
  if (a == NULL) abort();
  
  double *b = malloc(sizeof(double) * size * size);
  if (b == NULL) abort();
  
  double *c = malloc(sizeof(double) * size * size);
  if (c == NULL) abort();
  
  struct timespec start;
  
  fprintf(stderr, "uninterruptible_matrix_multiply\n");
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  uninterruptible_matrix_multiply(size, a, b, c);
  print_elapsed_time(&start);
  
  if (setjmp(interrupt_handler)) {
    fprintf(stderr, "flag interrupted!\n");
  } else {
    fprintf(stderr, "flag_matrix_multiply\n");
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    flag_matrix_multiply(size, a, b, c);
    print_elapsed_time(&start);
    
    interrupt_flag = true;
    
    fprintf(stderr, "flag_matrix_multiply interrupted\n");
    flag_matrix_multiply(size, a, b, c);
  }
  
  if (setjmp(interrupt_handler)) {
    fprintf(stderr, "write protection interrupted!\n");
  } else {
    fprintf(stderr, "write_matrix_multiply\n");
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    write_matrix_multiply(size, a, b, c);
    print_elapsed_time(&start);
    
    invalidate_poll_page();
    
    fprintf(stderr, "write_matrix_multiply interrupted\n");
    write_matrix_multiply(size, a, b, c);
  }
  
  if (setjmp(interrupt_handler)) {
    fprintf(stderr, "test protection interrupted!\n");
  } else {
    fprintf(stderr, "test_matrix_multiply\n");
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    test_matrix_multiply(size, a, b, c);
    print_elapsed_time(&start);
    
    invalidate_poll_page();
    
    fprintf(stderr, "test_matrix_multiply interrupted\n");
    test_matrix_multiply(size, a, b, c);
  }
  
  return 0;
}
