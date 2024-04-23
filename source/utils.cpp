/**
 * @file utils.cpp
 * @author Stefano Romeo
 * @brief Various functions and utilities 
 * @version 0.1
 * @date 2024-04-03
 * 
 */

#include "utils.hpp"
#include <cmath>

/**
 * @brief Simple function to calculate time of execution
 * 
 * This function is used to append a time to every output, this is used to have a timestamp and to trace when a certain operation has executed
 */
void calculate_time(){
    std::chrono::duration<double> elapsed = std::chrono::system_clock::now() - start;
	std::cout << elapsed.count() << "s ";
};

/**
 * @brief Find the position of a node inside the computational tree
 *
 * This function finds the position of a node in a n-th tree, inside the function child ranks are assigned to the child array
 * Childs are assigned using the formula:
 * 
 * @param nodeRank Rank of current node
 * @param totalRanks Total number of node in computational tree
 * @param treeWidth Width of the n-th tree
 * @param childs Child vector (uninizialized before entering the function)
 * @return first child rank to be used as an offset to send messages
 */
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

/**
 * @brief This function is used to find the level of a node inside the computational tree
 * 
 * Node level is used to calculate how many particles a node will recieve, for matchamker this level is the sum of all nodes below
 *
 * @param nodeRank Rank of current node
 * @param treeWidth With of the n-th tree
 * @param lastRank Rank of last node
 * @return Level of the node inside the tree 
 */
int findLevelInTree(int nodeRank, int treeWidth, int lastRank){
    int currentNode = nodeRank;
    int level = 0;

    while((currentNode = ceil((float)currentNode*treeWidth + 1)) <= lastRank){level++;}

    return ++level;
}

/**
 * @brief This function finds the level of the node in the case of a binary tree
 * 
 * @param nodeRank Rank of node
 * @return Level of node inside computational tree 
 */
int findLevelInBinaryTree(int nodeRank, int lastRank){
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
