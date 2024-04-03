/**
 * @file utils.hpp
 * @author Stefano Romeo
 * @brief File definition for different macros and tags
 * @version 0.1
 * @date 2024-04-03
 * 
 */

#ifndef UTILS
#define UTILS
#include <chrono>
#include <mpi.h>
#include <ctime>
#include <iostream>
#include <syncstream>

/**
 * @def OVERWORK 
 * This Tag is used to signal the OVERWORK status, in this status a node has too many particles for the current average, this should cause work stealing if there are any cores available
 */


/**
 * @def STABLE
 * This Tag signals STABLE status, in this status a node declares to be in line with the current average of data that every node has, this is an healthy state and does not cause any further
 * operation on the node
 * 
 */


/**
 * @def UNDERWORK
 * UNDERWORK status signals that the node has a lower number of particles and declares himself available to recieve data from other nodes
 */

/**
 * @def IDLE
 * IDLE Tag indicates that a core has fallen in an idle state and all the particles have been consumed, this should immediatly cause an allert between all cores and cause work stealing except
 * in the final phase of reduction
 */

/**
 * @def DATA
 * This tag is reserved to the final reduction indicating that the message sent contains data obtained by the reduction operatioThis tag is reserved to the final reduction indicating that the message sent contains data obtained by the reduction operation
 */

/**
 * @def AVERAGE
 * Tag reserved to average updates from matchmaker to a node below in the hierarchy
 */


/**
 * @def THRESHOLD
 * Tag reserved to threshold updates from matchmaker to a node below in the hierarchy
 */


/**
 * @def VICTIM
 * This tag signals that a node has been selected to be a victim of work stealing (the message containes the number of particles to be stolen)
 */

/**
 * @def TARGET
 * This tag signals that a node has been selected to be a target of work stealing (the message containes the number of particles to be injected)
 */

/**
 * @def LOCKED
 * This tag prevents any work stealing operation to appen on a node, when a node is locked, it's parent is also locked, effectivly stopping work stealing in a brach of the computational tree
 * To remove this status an UNLOCKED message has to follow, any other message not tagged as UNLOCKED will be ignored
 */

/**
 * @def UNLOCKED
 * This tag unlocks a core, when unlocking a core is not placed in any other status but it is able to communicate normally again after a workstealing event
 */

/**
 * @def COMM
 * Special tag used in communication of suppementary information inside a workstealing operation 
 */


/**
 * @def UPDATE
 * Special tag used to prevent wrong recieve operation in a matchmaker
 */

/**
 * @def MAX_STEAL
 * Indicates max number of particles to be stolen in a work stealing event, it is also an upper bound of the inWindowBuffer and outWindowBuffer
 */

/**
 * @def MIN_THRESHOLD
 * Defines the lower bound of the threshold metric, in order to prevent stealing events of little importance and to prevent stealing al low particle count 
 */


#define OVERWORK 1 
#define STABLE 2
#define UNDERWORK 3
#define IDLE 4
#define DATA 5
#define AVERAGE 6
#define THRESHOLD 7
#define VICTIM 8
#define TARGET 9
#define LOCKED 10
#define UNLOCKED 11
#define COMM 12
#define UPDATE 13
#define MAX_STEAL 50000
#define MIN_THRESHOLD 1000

extern std::chrono::time_point<std::chrono::system_clock> start;


void calculate_time();
int setPositionInTree(int nodeRank, int totalRanks, int treeWidth, int *childs);
int findInitialLeaf(int totalRanks, int treeWidth);
int findLevelInTree(int nodeRank, int treeWidth);
int findLevelInBinaryTree(int nodeRank);

#endif