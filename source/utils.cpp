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
 * @return parent rank
 */
int setPositionInTree(int nodeRank, int totalRanks, int* treeWidth, int* childs,int levels){

    
    //Setting child and parent for rank 0
    if(nodeRank == 0){
        for(int i = 0;i<treeWidth[0];i++){
            childs[i] = i+1;
        }

        return -1;
    }

    int x[levels-1];  //Initial rank of every level
    int z[levels-1];

    //Base case
    x[0] = 0;
    x[1] = 1 * treeWidth[0];

    z[0] = 0;
    z[1] = treeWidth[0];

    //Calculating succession
    for(int i = 2;i<levels;i++){
        x[i] = x[i-1] * treeWidth[i-1];
        z[i] = x[i-1] + x[i];
    }

    int nodeLevel = findLevelInTree(nodeRank, treeWidth, totalRanks, levels);

    //Calcolo dei figli 
    if(nodeLevel != levels - 1){
        int w = x[nodeLevel+1] / x[nodeLevel];

        int relRank = nodeRank - (z[nodeLevel-1]+1);
        for(int i = 0;i<w;i++){
            childs[i] = relRank * w + i + (z[nodeLevel]+1);
        }
    }    

    if(nodeLevel == 1){
        return 0;
    }

    //calcolo del parent
    int rev_w = x[nodeLevel] / x[nodeLevel-1];

    int relRank = nodeRank - (z[nodeLevel-1]+1);

    return (relRank/rev_w) + (z[nodeLevel-2]+1);
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
 /*
int findLevelInTree(int nodeRank, int treeWidth, int lastRank){
    int currentNode = nodeRank;
    int level = 0;

    while((currentNode = ceil((float)currentNode*treeWidth + 1)) <= lastRank){level++;}

    return ++level;
}
*/

int findLevelInTree(int nodeRank, int *treeWidth, int lastRank, int levels){

    int x[levels-1];  //Initial rank of every level
    int z[levels-1];

    if(nodeRank == 0){return 0;}

    //Base case
    x[0] = 0;
    x[1] = 1 * treeWidth[0];

    z[0] = 0;
    z[1] = treeWidth[0];

    //Calculating succession of nodes
    for(int i = 2;i<levels;i++){
        x[i] = x[i-1] * treeWidth[i-1];
        z[i] = x[i-1] + x[i];
    }

    for(int i = 1;i<levels;i++){
        if((float)nodeRank / z[i] <=1){
            return i;
        }
    }



    return 0;
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

int findInitialLeaf(int* treeWidth, int levels){
    int xn = 1 * treeWidth[0];
    int x_n1 = xn;

    for(int i = 1;i<levels-2;i++){
        x_n1 = xn + xn * treeWidth[i];
        xn = x_n1;
    }

    return xn+1;
}