#include "utils.hpp"
#include <cmath>

/**
 * @brief Semplice funzione per la temporizzazione
 * 
 * Questa funzione fa uso della libreria chrono e delle utilities fornite per aggiungere dei timestamp ai log su file
 * il tempo viene contato in millisecondi trascorsi dall'inizio della computazione determinata nel main
 */
void calculate_time(std::osyncstream &out){
    std::chrono::duration<double> elapsed = std::chrono::system_clock::now() - start;
	out << elapsed.count() << "s ";
};

int setPositionInTree(int nodeRank, int totalRanks, int treeWidth, int* childs){

    if(nodeRank * treeWidth + 1 <= totalRanks){
        for(int i = 0; i < treeWidth && nodeRank * treeWidth + 1 + i <= totalRanks;i++){
            childs[i] = nodeRank * treeWidth + 1 + i;     
        }
    }    

    if(nodeRank == 0){
        return -1;
    }else{
        return ceil((float)nodeRank/treeWidth - 1);
    }
}

int findLevelInTree(int nodeRank, int treeWidth){
    int currentNode = nodeRank;
    int level = 0;

    if(nodeRank == 0) return 0;
    while(ceil((float)nodeRank/treeWidth - 1) != 0){level++;}

    return level;
}

//Solo per test su albero binario
int findLevelInBinaryTree(int nodeRank){
    if(nodeRank == 0) return 3;
    if(nodeRank <= 2 && nodeRank != 0) return 2;
    return 1;
}

int findInitialLeaf(int totalRanks, int treeWidth){

    if(treeWidth != 2)
        return (totalRanks/(treeWidth - 1)) + 1;
    else
        return 3;
        //Attenzione, questa cosa è fatta solo per i test ci sono infinite motivazione per cui questo sia errato
        //I test sono fatti con un albero binario e i nodi worker sono sempre gli stessi, realmente non si userà mai un'albero binario
}
