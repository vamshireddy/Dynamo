/*
 * Header file for cache implementation.
 * Author: Vamshi Reddy Konagari (vkonagar@andrew.cmu.edu)
 * Date: 12/4/2016
 */
#ifndef PROXY_CACHE_H
#define PROXY_CACHE_H

#include <stdio.h>
#include <sys/time.h>
#include <pthread.h>
#include "util.h"

#define MAX_CACHE_SIZE          (10 * 1024 * 1024) /* 10 Mb */
#define CACHE_INSERT_ERR        -3
#define CACHE_DELETE_ERR        -4
#define CACHE_INSERT_SUCCESS    0
#define CACHE_DELETE_SUCCESS    0
#define MAX_KEY_LENGTH          1000

/* Key. For webserver, its library name */
typedef struct cache_key
{
    char key_data[MAX_KEY_LENGTH];
}cache_key_t;

/* Value. For webserver, its handle */
typedef struct cache_value
{
    void* value_data;
}cache_value_t;

typedef struct cache_data_item
{
    cache_key_t key;
    cache_value_t value;
}cache_data_item_t;

/* structure of a cache entry. Read write lock is used to protect this. */
typedef struct cache_entry
{
    cache_data_item_t* data;
    pthread_rwlock_t lock;
    int data_size;
    void (*delete_callback)(cache_data_item_t*); /* This is called when the
                                                    item is evicted from the
                                                    cache */
    struct timeval timestamp;
    struct cache_entry* next;
    struct cache_entry* prev;
}cache_entry_t;

typedef struct cache
{
    pthread_rwlock_t lock;
    cache_entry_t* head;
    int total_size;
}cache_t;

/* Create cache structures */
cache_t* get_new_cache();
cache_entry_t* get_new_cache_entry();

/* Put, Get, and Delete */
int add_to_cache(cache_t* cache, cache_entry_t* entry);
int delete_lru_entry(cache_t* cache);
cache_entry_t* get_cached_item_with_lock(cache_t* cache, cache_key_t* key);

/* Misc */
void display_cache();
void free_cache_entry(cache_entry_t* entry);

/* Global coarse rw locks */
void get_global_cache_wrlock(cache_t* cache);
void release_global_cache_wrlock(cache_t* cache);
#endif /* End of header */
