#include "kvs.h"

#include <ctype.h>
#include <stdlib.h>

#include "string.h"




// Checks if a key exists in the hashtable.
// @param ht Pointer to the hashtable.
// @param key Pointer to the string of the key to be checked.
// @return Returns 1 if the key exists, 0 otherwise.

int key_exists(HashTable *ht, const char *key) {
    // Verifica se a hashtable ou a chave fornecida são inválidas
    if (ht == NULL || key == NULL) {
        return 0; // Chave ou hashtable inválida, retorna 0
    }

    // Calcula o índice da chave na hashtable usando a função de hash
    int index = hash(key);
    if (index < 0 || index >= TABLE_SIZE) {
        return 0; // Índice fora dos limites da tabela
    }

    // Bloqueio para leitura para evitar conflitos com operações simultâneas
    pthread_rwlock_rdlock(&ht->tablelock);

    // Obtem o nó correspondente ao índice calculado
    KeyNode *current = ht->table[index];

    // Percorre a lista encadeada de nós na posição do índice
    while (current != NULL) {
        // Compara a chave atual com a chave fornecida
        if (strcmp(current->key, key) == 0) {
            // Se as chaves forem iguais, libera o bloqueio e retorna 1
            pthread_rwlock_unlock(&ht->tablelock);
            return 1; // Chave encontrada
        }
        current = current->next; // Move para o próximo nó
    }

    // Se a chave não for encontrada, libera o bloqueio e retorna 0
    pthread_rwlock_unlock(&ht->tablelock);
    return 0; // Chave não encontrada
}



// Hash function based on key initial.
// @param key Lowercase alphabetical string.
// @return hash.
// NOTE: This is not an ideal hash function, but is useful for test purposes of
// the project
int hash(const char *key) {
  int firstLetter = tolower(key[0]);
  if (firstLetter >= 'a' && firstLetter <= 'z') {
    return firstLetter - 'a';
  } else if (firstLetter >= '0' && firstLetter <= '9') {
    return firstLetter - '0';
  }
  return -1; // Invalid index for non-alphabetic or number strings
}

struct HashTable *create_hash_table() {
  HashTable *ht = malloc(sizeof(HashTable));
  if (!ht)
    return NULL;
  for (int i = 0; i < TABLE_SIZE; i++) {
    ht->table[i] = NULL;
  }
  pthread_rwlock_init(&ht->tablelock, NULL);
  return ht;
}

int write_pair(HashTable *ht, const char *key, const char *value) {
  int index = hash(key);

  // Search for the key node
  KeyNode *keyNode = ht->table[index];
  KeyNode *previousNode;

  while (keyNode != NULL) {
    if (strcmp(keyNode->key, key) == 0) {
      // overwrite value
      free(keyNode->value);
      keyNode->value = strdup(value);
      return 0;
    }
    previousNode = keyNode;
    keyNode = previousNode->next; // Move to the next node
  }
  // Key not found, create a new key node
  keyNode = malloc(sizeof(KeyNode));
  keyNode->key = strdup(key);       // Allocate memory for the key
  keyNode->value = strdup(value);   // Allocate memory for the value
  keyNode->next = ht->table[index]; // Link to existing nodes
  ht->table[index] = keyNode; // Place new key node at the start of the list
  return 0;
}

char *read_pair(HashTable *ht, const char *key) {
  int index = hash(key);

  KeyNode *keyNode = ht->table[index];
  KeyNode *previousNode;
  char *value;

  while (keyNode != NULL) {
    if (strcmp(keyNode->key, key) == 0) {
      value = strdup(keyNode->value);
      return value; // Return the value if found
    }
    previousNode = keyNode;
    keyNode = previousNode->next; // Move to the next node
  }

  return NULL; // Key not found
}

int delete_pair(HashTable *ht, const char *key) {
  int index = hash(key);

  // Search for the key node
  KeyNode *keyNode = ht->table[index];
  KeyNode *prevNode = NULL;

  while (keyNode != NULL) {
    if (strcmp(keyNode->key, key) == 0) {
      // Key found; delete this node
      if (prevNode == NULL) {
        // Node to delete is the first node in the list
        ht->table[index] =
            keyNode->next; // Update the table to point to the next node
      } else {
        // Node to delete is not the first; bypass it
        prevNode->next =
            keyNode->next; // Link the previous node to the next node
      }
      // Free the memory allocated for the key and value
      free(keyNode->key);
      free(keyNode->value);
      free(keyNode); // Free the key node itself
      return 0;      // Exit the function
    }
    prevNode = keyNode;      // Move prevNode to current node
    keyNode = keyNode->next; // Move to the next node
  }

  return 1;
}

void free_table(HashTable *ht) {
  for (int i = 0; i < TABLE_SIZE; i++) {
    KeyNode *keyNode = ht->table[i];
    while (keyNode != NULL) {
      KeyNode *temp = keyNode;
      keyNode = keyNode->next;
      free(temp->key);
      free(temp->value);
      free(temp);
    }
  }
  pthread_rwlock_destroy(&ht->tablelock);
  free(ht);
}
