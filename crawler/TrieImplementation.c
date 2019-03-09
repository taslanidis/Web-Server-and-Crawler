#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "Trie.h"
#include "math.h"
#include "sys/ioctl.h"
#include "unistd.h"
#include "sys/time.h"
#include "time.h"

#define MaxWords 10
#define k1 1.2
#define b 0.75


Trie* root;
int new_word;                                                                   //is used as a flag to determine when a word ends
Trie* current;


void CreateTrie(){
    root = NULL;
    current = root;
}

void Insert(char buffer, int document_id, int fileid, char* DocName){
    TrieInsert(buffer, document_id, fileid, DocName);
}

int CheckValidity(char buffer){
    //below commented area is an alternative, where only pure words will be passed in trie
    //if((buffer != ' ') && (buffer != '\t') && (buffer != '.') && (buffer != '\n') && (buffer != ',') && (buffer != '/'))
        //if((buffer != '(') && (buffer != ')') && (buffer != '`') && (buffer != ';') && (buffer != '<') && (buffer != '>') && (buffer != ':'))
            //if((buffer != '?') && (buffer != '!') && (buffer != '^') && (buffer != '&') && (buffer != '"') && (buffer != '[') && (buffer != ']'))
                //return 1;
    if((buffer != ' ') && (buffer != '\t'))
        return 1;
    return 0;
}

void TrieInsert(char buffer, int document_id, int fileid, char* DocName){
    if(CheckValidity(buffer)){                                                  //check what the char in buffer is, in case of tab or whitespace returns 0
        if (current == NULL){                                                   //current = null is the simplest case where I just create a new node
            current = malloc(sizeof(Trie));
            current->Entry = buffer;
            current->Next = NULL;
            current->SamePrefix = NULL;
            current->PostingList = NULL;
            if(root == NULL)    root = current;
        }else{
            if(new_word == 0){                                                  //if new word = 0 means this is the first letter so we are scanning at root level
                Trie* previous = NULL;
                int eol = 1;                                                    //flag for end of list
                while(current->Entry < buffer){                                 //alphabetically sorting
                    previous = current;
                    if((current->SamePrefix) != NULL){
                        current = current->SamePrefix;
                    }else{
                        eol = 0;
                        break;
                    }
                }
                if(current->Entry != buffer){
                    if(previous == current){                                    //common case between nodes
                        Trie* newnode = malloc(sizeof(Trie));
                        newnode->Entry = buffer;
                        newnode->SamePrefix = current->SamePrefix;
                        newnode->Next = NULL;
                        newnode->PostingList = NULL;
                        current->SamePrefix = newnode;
                        current = newnode;
                    }else if(previous == NULL){                                 //first node in list
                        Trie* newnode = malloc(sizeof(Trie));
                        newnode->Entry = buffer;
                        newnode->SamePrefix = current;
                        newnode->Next = NULL;
                        newnode->PostingList = NULL;
                        root = newnode;
                        current = newnode;
                    }else if(eol == 1){                                         //end of list
                        Trie* newnode = malloc(sizeof(Trie));
                        newnode->Entry = buffer;
                        newnode->SamePrefix = current;
                        newnode->PostingList = NULL;
                        newnode->Next = NULL;
                        previous->SamePrefix = newnode;
                        current = newnode;
                    }else{                                                      //last item on list
                        Trie* newnode = malloc(sizeof(Trie));
                        newnode->Entry = buffer;
                        newnode->SamePrefix = NULL;
                        newnode->PostingList = NULL;
                        newnode->Next = NULL;
                        previous->SamePrefix = newnode;
                        current = newnode;
                    }
                }else{
                                                                                //this letter already belongs in this Trie
                    new_word = 1;
                    return;
                }
            }else{                                                              //simple insertion - not in root level
                if(current->Next == NULL){
                    Trie* newnode = malloc(sizeof(Trie));
                    newnode->Entry = buffer;
                    newnode->SamePrefix = NULL;
                    newnode->PostingList = NULL;
                    newnode->Next = NULL;
                    current->Next = newnode;
                    current = newnode;
                }else{                                                          //same code as above
                    current = current->Next;
                    Trie* previous = NULL;
                    int eol = 1;                                                //flag for end of list
                    while(current->Entry < buffer){
                        previous = current;
                        if((current->SamePrefix) != NULL){
                            current = current->SamePrefix;
                        }else{
                            eol = 0;
                            break;
                        }
                    }
                    if(current->Entry != buffer){                               //if it doesn't already exist
                        if(previous == current){                                //common case-somewhere inside the list between nodes
                            Trie* newnode = malloc(sizeof(Trie));
                            newnode->Entry = buffer;
                            newnode->SamePrefix = current->SamePrefix;
                            newnode->Next = NULL;
                            newnode->PostingList = NULL;
                            current->SamePrefix = newnode;
                            current = newnode;
                        }else if(previous == NULL){                             //case for the first in list
                            Trie* newnode = malloc(sizeof(Trie));               //copy current on new node, and current goes first
                            newnode->Entry = current->Entry;
                            newnode->SamePrefix = current->SamePrefix;
                            newnode->Next = current->Next;
                            newnode->PostingList = current->PostingList;
                            current->Entry = buffer;
                            current->SamePrefix = newnode;
                            current->Next = NULL;
                            current->PostingList = NULL;
                        }else if(eol == 1){                                     //reached end of prefix list
                            Trie* newnode = malloc(sizeof(Trie));
                            newnode->Entry = buffer;
                            newnode->SamePrefix = current;
                            newnode->PostingList = NULL;
                            newnode->Next = NULL;
                            previous->SamePrefix = newnode;
                            current = newnode;
                        }else{                                                  //last item on list
                            Trie* newnode = malloc(sizeof(Trie));
                            newnode->Entry = buffer;
                            newnode->SamePrefix = NULL;
                            newnode->PostingList = NULL;
                            newnode->Next = NULL;
                            previous->SamePrefix = newnode;
                            current = newnode;
                        }
                    }else{                                                      //this letter already belongs in this Trie
                        new_word = 1;
                        return;
                    }
                }
            }
        }
        new_word = 1;
    }else{                                                                      //space-tabs indicate the end of some word
        if(new_word == 1){                                                      //if it the first space after some letter- and not space after space
            int temp_amount = 0;
            if(current->PostingList != NULL){                                   //if the posting list exists
                PList* PostingList = current->PostingList;
                int found = 1;
                if(PostingList->First != NULL){                                 //not empty posting list
                    PListNode* PostingNode = PostingList->First;
                    while((strcmp(PostingNode->DocName,DocName) != 0) && (PostingNode->Doc_id != document_id)){                           //search in PList
                        if(PostingNode->Next == NULL){
                            found = 0;
                            break;
                        }
                        PostingNode = PostingNode->Next;
                    }

                    if(found == 1){                                             //Plist node for this document already exists
                        PostingNode->Amount++;
                    }else{
                        PListNode* newnode = malloc(sizeof(PListNode));
                        newnode->Amount = 1 + temp_amount;
                        newnode->Next = NULL;
                        newnode->Doc_id = document_id;
                        newnode->File_id = fileid;
                        newnode->DocName = malloc((strlen(DocName) + 1)*sizeof(char));
                        strcpy(newnode->DocName, DocName);
                        PostingNode->Next = newnode;
                        PostingList->Total++;
                    }
                }else{                                                          //first plist node creation
                    PListNode* newnode = malloc(sizeof(PListNode));
                    newnode->Amount = 1;
                    newnode->Next = NULL;
                    newnode->Doc_id = document_id;
                    newnode->File_id = fileid;
                    newnode->DocName = malloc((strlen(DocName) + 1)*sizeof(char));
                    strcpy(newnode->DocName, DocName);
                    PostingList->First = newnode;
                    PostingList->Total ++;
                }
            }else{                                                              //PostingList doesn't exist yet, so I create it
                current->PostingList = malloc(sizeof(PList));
                (current->PostingList)->Total = 1;
                ((current->PostingList)->First) = malloc(sizeof(PListNode));
                ((current->PostingList)->First)->Doc_id = document_id;
                ((current->PostingList)->First)->File_id = fileid;
                ((current->PostingList)->First)->DocName = malloc((strlen(DocName) + 1)*sizeof(char));
                strcpy(((current->PostingList)->First)->DocName, DocName);
                ((current->PostingList)->First)->Amount = 1;
                ((current->PostingList)->First)->Next = NULL;
            }
            current = root;
            new_word = 0;                                                       //flag to know when I'm going to consider a space as the end of some word
        }
    }
}

Trie* GetRoot(){
    return root;
}


void df(Trie* node, char* word, int size, FILE* fp2){

    if(node->PostingList != NULL){                                              //writes the word inside the file fp2
        for(int j = 0; j < size; j++){
            fprintf(fp2, "%c", word[j]);
        }
        fprintf(fp2, "%c", node->Entry);
        fprintf(fp2, "    %d\n", (node->PostingList)->Total);
    }
    if(node->SamePrefix != NULL)
        df(node->SamePrefix, word, size, fp2);
    if(node->Next != NULL){
        word[size] = node->Entry;
        size++;
        df(node->Next, word, size, fp2);
    }
}

PListNode* dfSingle(char* word, int offset){                                    //recursive function using global variable current to determine the current node
    if(offset == 0) current = root;                                             //there is no problem here using global var in recursion because we only go forwards
    if(current == NULL) return NULL;
    while(current->Entry < word[offset]){                                       //in order to find the word and return the amount
        if(current->SamePrefix != NULL){
            current = current->SamePrefix;
        }else{
            return NULL;                                                        //the word doesn't exist
        }
    }

    if(offset == (strlen(word) - 1)){
        if(current->PostingList != NULL){
            return (current->PostingList)->First;
        }else{
            return NULL;                                                        //the word doesn't exist
        }
    }else if(current->Entry == word[offset]){
        offset++;
        current = current->Next;
        return dfSingle(word,offset);                                           //recursion
    }else{
        return NULL;
    }
}

double my_clock(void) {                                                         //function to return current clock
    struct timeval t;
    gettimeofday(&t, NULL);
    return (1.0e-6*t.tv_usec + t.tv_sec);
}

void TrieDelete(Trie* node){                                                    //It is immplemented to delete from last element to first
                                                                                //and since it is alphabetically sorted from max ascii value to min
    if(node->PostingList != NULL){                                              //first deleting the posting list
        PListNode* pl = (node->PostingList)->First;
        PListNode* temp;
        while(pl->Next != NULL){
            temp = pl;
            pl = pl->Next;
            free(temp->DocName);
            free(temp);
        }
        free(pl->DocName);
        free(pl);
        free(node->PostingList);
    }
    if(node->SamePrefix != NULL)                                                //call for the same prefix node
        TrieDelete(node->SamePrefix);
    if(node->Next != NULL){                                                     //call for the next node
        TrieDelete(node->Next);
    }

    free(node);
    return;
}
