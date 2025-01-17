#ifndef KEY_VALUE_STORE_H
#define KEY_VALUE_STORE_H
#define TABLE_SIZE 26

#include <pthread.h>
#include <stddef.h>

typedef struct KeyNode {
  char *key;
  char *value;
  struct KeyNode *next;
} KeyNode;

typedef struct HashTable {
  KeyNode *table[TABLE_SIZE];
  pthread_rwlock_t tablelock;
} HashTable;

/// Creates a new KVS hash table.
/// @return Newly created hash table, NULL on failure
struct HashTable *create_hash_table();

int hash(const char *key);

// Writes a key value pair in the hash table.
// @param ht The hash table.
// @param key The key.
// @param value The value.
// @return 0 if successful.
int write_pair(HashTable *ht, const char *key, const char *value);

// Reads the value of a given key.
// @param ht The hash table.
// @param key The key.
// return the value if found, NULL otherwise.
char *read_pair(HashTable *ht, const char *key);

/// Deletes a pair from the table.
/// @param ht Hash table to read from.
/// @param key Key of the pair to be deleted.
/// @return 0 if the node was deleted successfully, 1 otherwise.
int delete_pair(HashTable *ht, const char *key);

/// Frees the hashtable.
/// @param ht Hash table to be deleted.
void free_table(HashTable *ht);

//Verifica se uma chave existe na hashtable.
// @param ht Ponteiro para a hashtable.
// @param key Ponteiro para a string da chave a ser verificada.
// @return Retorna 1 se a chave existe, 0 caso contrário.

int key_exists(HashTable *ht, const char *key);

#endif // KVS_H
