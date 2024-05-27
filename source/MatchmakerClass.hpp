/**
 * @file MatchmakerClass.hpp
 * @author Stefano Romeo
 * @brief Matchmaker base model to create further specialization and to communicate messages inside the tree
 * @version 0.1
 * @date 2024-04-05
 */

#include "utils.hpp"
#include "mpi.h"
#include "Node.hpp"
#include <vector>
#include <random>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <cstring>
#include <shared_mutex>
#include <mutex>
#include <syncstream>
#include <unistd.h>


using namespace std;

class Matchmaker : public Node{

    protected:

    int childNumber;            ///<Number of childs of a matchmaker
    int *childRanks;            ///<Ranks of childs
    int *tagArray;              ///<Current status of childs
    int *valueArray;            ///<Current particle number of childs
    int lastVictimRank;         ///<Last victim rank
    int lastTargetRank;         ///<Last target rank
    int offset;                 ///<Offset of rank for child
    float lowerAverage;         ///<Average of lower nodes
    int lowerThreshold;         ///<Threshold of lower nodes


        /**
         * @brief Recieves a messagge from a child in a blocking function
         * 
         * Recieves a message from a child using an MPI blocking function, this sets recvFlag with two parameters:
         *  - Total particles : The first element of the vector contains the current number of particles in possession of a node
         *  - Status : Declaration of status of child node
         * @param recvFlag Contains the 2 items sent back by the child node as an update
         * @return Status indicating UPDATE tag and rank of sender
         */
        MPI_Status waitControlMessages(int *recvFlag){
            MPI_Status stat;

            MPI_Recv(recvFlag, 2, MPI_INT, MPI_ANY_SOURCE, UPDATE, MPI_COMM_WORLD, &stat);
            return stat;
        }

        /**
         * @brief Find available victim for work stealing
         * 
         * @param victim 
         * @return rank of victim node found, -1 if no node available is found 
         */
        int findPossibleVictim(int victim){
            
            for(int i = 0;i < childNumber; i++){
                if(tagArray[i] == OVERWORK && i != victim)
                    return i+offset;
            }

            return -1;
        }

        /**
         * @brief Find available target for work stealing
         * 
         * @param target 
         * @return Rank of target node found, -1 if no node available is found
         */
        int findPossibleTarget(int target){
            for(int i = 0;i < childNumber; i++){
                if(tagArray[i] == IDLE && i != target)
                    return i+offset;
            }

            for(int i = 0;i < childNumber; i++){
                if(tagArray[i] == UNDERWORK && i != target)
                    return i+offset;
            }

            return -1;
        }

        /**
         * @brief Check number of still elaborating ranks
         * 
         *To prevent deadlock inside main function loop messages are recieved only if active processes are still elaborating
         *data under a matchmaker inside computational tree, node in LOCKED status are considered still elaborating
         *
         * @return int 
         */
        int checkActiveProcesses(){
            int counter = 0;
            for(int i = 0;i<childNumber;i++){
                if(valueArray[i] > 0 || tagArray[i] == LOCKED){
                    counter++;
                }
            }

            return counter;
        }

        /**
         * @brief Checks whether a node is in LOCKED status
         * 
         * @return true if any child node is in LOCKED status
         * @return false all nodes are unlocked
         */
        bool anyNodeIsLocked(){
            for(int i=0;i<childNumber;i++){
                if(tagArray[i] == LOCKED){
                    return true;
                }
            }

            return false;
        }

        /**
         * @brief Updates current average
         * 
         *Updates average of particles in lower nodes, if average reaches zero value 1 is communicated instead to prevent
         *early termination 0 is communicated only when upper level matchmakers communicate 0 value average
         */
        virtual void updateAverage(){
            float localSum = 0;
            int activeNodes = 0;

            for(int i = 0;i<childNumber;i++){
                localSum += valueArray[i];
            }

            //Attenzione potrebbe verificarsi una race condition
            if(localSum != 0)
                lowerAverage = localSum/childNumber;
            else
               lowerAverage = 1;

            totalParticles = localSum;
        }

        /**
         * @brief Helper function to modify tagArray and valueArray
         * 
         * @param source Source rank of child sender
         * @param localFlag Set of values containing new status and new particle numbers
         *
         * This function can be fixed by making so that the worker node does not send status updates
         * the status of a node can be therefore be determined by the matchmaker so to make the system
         * more stable and simplify communications by sending only one value
         */
        void updateValues(int source, int *localFlag){
            tagArray[source - offset] = localFlag[1];
            valueArray[source - offset] = localFlag[0];
        }

        /**
         * @brief Notifies new average value to a rank
         * 
         * @param reciever Rank of reciever
         */
        void notifyAverage(int reciever){
            
            MPI_Send(&(this->lowerAverage), 1, MPI_FLOAT, reciever, AVERAGE, MPI_COMM_WORLD);
        }

        /**
         * @brief Notifies threshold to all child ranks
         * 
         */
        void notifyThreshold(){

            for(int i = 0;i<childNumber;i++){
                MPI_Send(&(this->lowerThreshold), 1, MPI_INT, childRanks[i], THRESHOLD, MPI_COMM_WORLD);
            }
        }

        /**
         * @brief Check if double stealing event has append and increases threshold
         * 
         * @param currentVictimRank Victim of current stealing event
         * @param currentTargetRank Target of current stealing event
         */
        void checkForDoubleSteal(int currentVictimRank, int currentTargetRank){
            if((currentVictimRank == lastVictimRank && currentTargetRank == lastTargetRank) || (currentVictimRank == lastTargetRank && currentTargetRank == lastVictimRank)){
                lowerThreshold *= 2;
                notifyThreshold();
            }

            lastVictimRank = currentVictimRank;
            lastTargetRank = currentTargetRank;
        }

        /**
         * @brief Calculates stealing quantity for stealing event
         * 
         * @param targetIndex Index of target inside value array
         * @param victimIndex Index of victim inside value array
         * @return Number of items to steal
         */
        int setStealingQuantity(int targetIndex, int victimIndex){

            int targetDistance = abs(lowerAverage - valueArray[targetIndex]);
            int victimDistance = abs(lowerAverage - valueArray[victimIndex]);

            return ((targetDistance + victimDistance)/2);
        }

        /**
         * @brief Writes to standard output current values of child ranks and their current status
         * 
         */
        void printMetrics(){
            cout << "MATCHMAKER " << nodeRank << " DECLARES METRICS " << endl << "\t THRESHOLD : " << lowerThreshold << endl << "\t AVERAGE : " << lowerAverage << endl << "\tTOTAL PARTICLES : " << totalParticles << endl;
            cout << "\t VALUES : " << endl;
            for(int i = 0;i < childNumber;i++){
                cout << "\t\tRANK : " << i+offset << " -> " << valueArray[i] << " -- " << tagArray[i] << endl;
            }

            cout << endl;
        }

        /**
         * @brief Deletes data from child nodes
         *
         * This function begins a multiple stealing event, when this has ended and data is gathered data is sent upwards, after all lower ranks are declared UNLOCKED
         * matchmaker is also declared UNLOCKED and more work stealing can happen
         * 
         * @param stealingQuantity Quantity of particles to be stolen
         */
        virtual void deleteDataFromNode(int stealingQuantity){
            cout << "DELETING : " << stealingQuantity << " FROM NODE : " << nodeRank << endl; 
            int actualSteal = gatherData(stealingQuantity);

            cout << "RANK : " << nodeRank << " SENDING ALL CLEAR TO PARENT..." << endl;
            MPI_Send(&actualSteal, 1, MPI_INT, parentRank, COMM, MPI_COMM_WORLD);
            cout << "SENT : " << actualSteal << " PARTICLES UPWARDS" << endl;
            while(anyNodeIsLocked()){}
            declareStatus(UNLOCKED);
        }

        /**
         * @brief Injects data in child nodes
         * 
         * Data is sent to a matchmaker and distributed across all child nodes, after child nodes are all declared as UNLOCKED matchmaker aslo declares UNLOCKED status
         * @param stealingQuantity Quantity of particles to be injected
         */
        virtual void injectDataInNode(int stealingQuantity){
            cout << "INJECTING : " << stealingQuantity << " IN NODE : " << nodeRank << endl;
            distributeData(stealingQuantity);

            cout << "SENT DATA TO LOWER NODES" << endl;
            while(anyNodeIsLocked()){}
            declareStatus(UNLOCKED);
        }


        /**
         * @brief Distributes data among child nodes
         *
         * Data is distributed to child nodes, child nodes are then locked after injecting the data, the distribution of data is uniform in the case
         * of distribution so that every child recieves the same amount of particles
         * 
         * @param stealingQuantity Quantity of data to be distributed
         */
        virtual void distributeData(int stealingQuantity){
            int *stealingArray = calculateStealing(stealingQuantity, true);
            int arrayOffset = 0;
            int temp = 0;

            for(int i=0;i<childNumber;i++){
                tagArray[i] = LOCKED;
                cout << "DISTRIBUTING : " << stealingArray[i] << " PARTICLES TO : " << childRanks[i] << endl;
                MPI_Win_lock(MPI_LOCK_EXCLUSIVE, childRanks[i], 0, inWindow);
                MPI_Put(inWindowBuffer+arrayOffset, stealingArray[i], MPI_INT, childRanks[i], 0, *(stealingArray+i), MPI_INT, inWindow);
                MPI_Win_unlock(childRanks[i], inWindow);

                arrayOffset += stealingArray[i];

                MPI_Send(stealingArray+i, 1, MPI_INT, childRanks[i], TARGET, MPI_COMM_WORLD);
            }
            totalParticles+=stealingQuantity;
            delete[] stealingArray;
        }
        
        /**
         * @brief Calculate particle distribution among nodes in a multiple stealing event
         * 
         * @param stealingQuantity Quantity of data to be delocated
         * @param evenDistribute True if distribution is uniform (Only when injecting data)
         * @return Array of ints containing number of particles to inject or steal for every core 
         */
        int *calculateStealing(int stealingQuantity, bool evenDistribute = false){
            float *percentages = new float[childNumber];
            int *stealingValues = new int[childNumber];

            for(int i=0;i<childNumber;i++){
                if(!evenDistribute){
                    if(valueArray[i] != 0)
                        percentages[i] = ((float)valueArray[i]/totalParticles);
                    else
                        percentages[i] = 0;
                }
                else
                    percentages[i] = (float)1/childNumber;
                
                stealingValues[i] = stealingQuantity * percentages[i];

                if(!evenDistribute)
                    cout << "STEALING : " << stealingValues[i] << " FROM NODE : " << i+offset << " WITH PERCENTAGE OF : " << percentages[i] << endl;
                else
                    cout << "INJECTING : " << stealingValues[i] << " IN NODE : " << i+offset << " WITH PERCENTAGE OF : " << percentages[i] << endl;
            }

            delete[] percentages;
            return stealingValues;
        }

        /**
         * @brief Gathers data from lower nodes
         *
         * Gathers data from child ranks, before gathering lockes the nodes so that no other stealing event can happen
         * then puts stolen data in buffer and signals all clear to upper rank
         * 
         * @param stealingQuantity Quantity to be stolen
         * @return Total number of particles stolen from childs
         */
        virtual int gatherData(int stealingQuantity){
            int *stealingArray = calculateStealing(stealingQuantity);
            int *tempArray = new int[MAX_STEAL];
            int arrayOffset = 0;
            int completeSteal = 0;
            int actualSteal = 0;

            if(anyNodeIsLocked()){
                return 0;
            }

            for(int i = 0;i<childNumber;i++){
                tagArray[i] = LOCKED;
                MPI_Send(stealingArray+i, 1, MPI_INT, childRanks[i], VICTIM, MPI_COMM_WORLD);
                MPI_Recv(&actualSteal, 1, MPI_INT, childRanks[i], COMM, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                MPI_Win_lock(MPI_LOCK_EXCLUSIVE, childRanks[i], 0, outWindow);
                MPI_Get(outWindowBuffer, stealingArray[i], MPI_INT, childRanks[i], sizeof(int), stealingQuantity, MPI_INT, outWindow);
                memcpy(outWindowBuffer+arrayOffset, tempArray, actualSteal);
                MPI_Win_unlock(childRanks[i], outWindow);
                completeSteal += actualSteal;
            }

            cout << "RANK : " << nodeRank << "HAS STOLEN : " << completeSteal << " PARTICLES FROM HIS CHILDS" << endl;

            memcpy(tempArray, outWindowBuffer, completeSteal);
            return completeSteal;
        }

        /**
         * @brief Steals data from a node
         * 
         * If victim node is a matchmaker data is gathered from all underlying nodes and sent upwards, this function
         * deals with stealing data from a single source and depositing them inside the input buffer to be sent
         *
         * @param stealingQuantity Quantity of data to be stolen
         * @param victimRank Rank of victim node
         * @return Quantity of data effectivly stolen
         */
        virtual int stealFromVictim(int *stealingQuantity, int victimRank){
            int actualSteal = 0;

            if(*stealingQuantity > MAX_STEAL)
                *stealingQuantity = MAX_STEAL;

            tagArray[victimRank - offset] = LOCKED;
            cout << "NODE RANK : " << nodeRank << " IS STEALING FROM BRANCH WITH ROOT : " << victimRank << endl;
            MPI_Send(stealingQuantity, 1, MPI_INT, victimRank, VICTIM, MPI_COMM_WORLD);
            MPI_Recv(&actualSteal, 1, MPI_INT, victimRank, COMM, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            cout << "NODE RANK : " << nodeRank << " RECIEVED ALL CLEAR TO STEAL : " << actualSteal << " PARTICLES" << endl;

            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, victimRank, 0, outWindow);
            MPI_Get(outWindowBuffer, actualSteal, MPI_INT, victimRank, 0, actualSteal, MPI_INT, outWindow);
            MPI_Win_unlock(victimRank, outWindow);

            cout << "ROOT OF STEALING HAS STOLEN : " << actualSteal << " PARTICLES" << endl;
            memcpy(inWindowBuffer, outWindowBuffer, actualSteal);
            return actualSteal;
        }

        /**
         * @brief Sends stolen data to a single target
         * 
         * After stealing data is sent to a single source, target is locked before the procedure
         *
         * @param stealingQuantity Quantity of data to send
         * @param targetRank Rank of target
         */
        virtual void sendToTarget(int *stealingQuantity, int targetRank){
            if(*stealingQuantity == 0){
                cout << "WILL NOT SEND DATA TO TARGET" << endl;
                return;
            }

            tagArray[targetRank - offset] = LOCKED;

            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, targetRank, 0, inWindow);
            MPI_Put(inWindowBuffer, *stealingQuantity, MPI_INT, targetRank, 0, *stealingQuantity, MPI_INT, inWindow);
            MPI_Win_unlock(targetRank, inWindow);

            MPI_Send(stealingQuantity, 1, MPI_INT, targetRank, TARGET, MPI_COMM_WORLD);
        }

        /**
         * @brief Checks termination flag
         * 
         * @return 1 if done flag is true
         */
        virtual int checkTermination(){
            return done;
        }

        /**
         * @brief Activates status thread
         * 
         * @return Status thread object
         */
        virtual thread activateStatusThread(){
            return thread(&Node::sendStatusFunction, this);
        }


        /**
         * @brief Activates reciever thread
         * 
         * @return Reciever thread object
         */
        virtual thread activateRecieverThread(){
            return thread(&Node::recieveMessageFromMatchmaker, this);
        }

        /**
         * @brief Begins ending procedure, in a normal matchmaker this is just a simple alert
         * 
         */
        virtual void endingProcedure(){
            cout << "RANK : " << nodeRank << " HAS ENDED COMPUTATION... " << endl;
        }


    public:

        /**
         * @brief Begins matchmaker main loop
         *
         * Begins matchmaker main loop, this recieves messages and sets values until termination is reached
         * When termination is reached alla lower nodes are alerted and computation is terminated
         * 
         * @param globalResult Result of reduction
         */
        void matchmakerMainLoop(int * globalResult){

            int *localFlag = new int[2];		//Conterrà il valore della media locale del singolo nodo
            MPI_Status stat;					//Status della richiesta, ogni comunicazione con un nodo viene salvata qui in modo da poter accedere all
                                                //info di quello specifico nodo, regola chi ha mandato la richiesta e l'aggiornamento del suo status
            int idlePenalty = lowerThreshold / 2;
            int stealingCounter = 0;

            thread statusThread;
            thread recieverThread;

            if(nodeRank != 0){
                statusThread = activateStatusThread();
                recieverThread = activateRecieverThread();
            }

            while(true){
                if(checkTermination())
                    break;

                if(checkActiveProcesses() > 0){

                    printMetrics();
                    stat = waitControlMessages(localFlag);


                    if(localFlag[1] == LOCKED){
                        tagArray[stat.MPI_SOURCE - offset] = LOCKED;
                        calculate_time();
                        cout << "NODE RANK : " << nodeRank << "RECIEVES MESSAGE FROM : " << stat.MPI_SOURCE << " OF LOCKING" << endl;
                    }

                    if(localFlag[1] == OVERWORK && tagArray[stat.MPI_SOURCE-offset] != LOCKED){
                        calculate_time();
                        cout << nodeRank << " CORE : " << stat.MPI_SOURCE << " IS OVERWORKED WITH : " << localFlag[0] << " PARTICLES" << endl;
                        updateValues(stat.MPI_SOURCE, localFlag);
                        updateAverage();
                        notifyAverage(stat.MPI_SOURCE);
                        int target = findPossibleTarget(stat.MPI_SOURCE - offset);

                        if(target != -1){
                            stealingCounter++;
                            //Entrambi meno uno perche si converte da rank a indice del vettore dei valori
                            int quantity = setStealingQuantity(target - offset, stat.MPI_SOURCE - offset);
                            calculate_time();
                            cout << "CORE : " << stat.MPI_SOURCE << " SHOULD LET " << quantity << " OBJECTS BE STOLEN BY : " << target << endl;
                            int actualStolen = stealFromVictim(&quantity, stat.MPI_SOURCE);
                            calculate_time();
                            cout << "ACTUAL STOLEN : " << actualStolen << endl;
                            sendToTarget(&actualStolen, target);
                            //checkForDoubleSteal(stat.MPI_SOURCE, target);
                            calculate_time();
                        }
                    }

                    if(localFlag[1] == STABLE && tagArray[stat.MPI_SOURCE-offset] != LOCKED){
                        calculate_time();
                        cout << "CORE : " << stat.MPI_SOURCE << " IS STABLE WITH " << localFlag[0] << " PARTICLES" << endl;
                        updateValues(stat.MPI_SOURCE, localFlag);
                        updateAverage();
                        notifyAverage(stat.MPI_SOURCE);
                    }

                    if(localFlag[1] == UNDERWORK && tagArray[stat.MPI_SOURCE - offset] != LOCKED){
                        calculate_time();
                        cout << "CORE : " << stat.MPI_SOURCE << " IS UNDERWORKED WITH " << localFlag[0] << " PARTICLES" << endl;
                        updateValues(stat.MPI_SOURCE, localFlag);
                        updateAverage();
                        notifyAverage(stat.MPI_SOURCE);
                        int victim = findPossibleVictim(stat.MPI_SOURCE - offset);

                        if(victim != -1){
                            stealingCounter++;
                            int quantity = setStealingQuantity(stat.MPI_SOURCE-offset, victim-offset);
                            calculate_time();
                            cout << "CORE : " << stat.MPI_SOURCE << " SHOULD STEAL FROM " << victim << " " << quantity << " OBJECTS" << endl;
                            int actualStolen = stealFromVictim(&quantity, victim);
                            calculate_time();
                            cout << "ACTUAL STOLEN : " << actualStolen << " FROM RANK : " << victim << endl;
                            sendToTarget(&actualStolen, stat.MPI_SOURCE);
                            //checkForDoubleSteal(victim, stat.MPI_SOURCE);
                            calculate_time();
                        }
                    }

                    if(localFlag[1] == IDLE && tagArray[stat.MPI_SOURCE-offset] != LOCKED){
                        calculate_time();
                        cout << "CORE : " << stat.MPI_SOURCE << " IS IDLE WITH : " << localFlag[0] << " PARTICLES" << endl;
                        updateValues(stat.MPI_SOURCE, localFlag);
                        calculate_time();

                        if(lowerThreshold/2 > MIN_THRESHOLD){
                            lowerThreshold = lowerThreshold/2;
                            notifyThreshold();
                        }

                        calculate_time();
                        cout << "TRESHOLD DECREASES TO : " << lowerThreshold << endl;
                        updateAverage();
                        calculate_time();
                        cout << "A WORKER HAS GONE IDLE! NOTIFY AVERAGE TO ALL WORKERS " << endl;
                        for(int i = 0;i<childNumber;i++){
                            calculate_time();
                            cout << "SENDING AVERAGE OF : " << lowerAverage << " TO : " << childRanks[i] << endl;
                            notifyAverage(childRanks[i]);
                        }

                        int victim = findPossibleVictim(stat.MPI_SOURCE - offset);

                        if(victim != -1){
                            stealingCounter++;
                            int quantity = setStealingQuantity(stat.MPI_SOURCE-offset, victim-offset);
                            calculate_time();
                            cout << "CORE : " << stat.MPI_SOURCE << " SHOULD IMMEDIATLY STEAL FROM " << victim << " " << quantity << " OBJECTS" << endl;
                            int actualStolen = stealFromVictim(&quantity, victim);
                            calculate_time();
                            cout << "ACTUAL STOLEN : " << actualStolen << " FROM RANK : " << victim << endl;
                            sendToTarget(&actualStolen, stat.MPI_SOURCE);
                            //checkForDoubleSteal(victim, stat.MPI_SOURCE);
                        }
                    }

                    if(localFlag[1] == UNLOCKED){
                        calculate_time();
                        cout << "-------CORE : " << stat.MPI_SOURCE << " IS NOW AVAILABLE FOR STEALING " << endl;
                        updateValues(stat.MPI_SOURCE, localFlag);

                        for(int i = 0;i<childNumber;i++){
                            if(tagArray[i] != LOCKED){
                                updateAverage();
                                notifyAverage(stat.MPI_SOURCE);
                            }
                        }
                    }
                }
            }		

            printMetrics();
            calculate_time();
            cout << "MATCHMAKER WITH RANK : " << nodeRank << " INITIATING ENDING PROCEDURES..." << endl;

            lowerAverage = 0;
            for(int i = 0;i<childNumber; i++){
                calculate_time();
                cout << "SENDING TO : " << childRanks[i] << endl;
                notifyAverage(childRanks[i]);
            }

            endingProcedure();

            if(nodeRank != 0){
                cout << "----STATUS : " << statusThread.joinable() << " IN RANK : " << nodeRank << endl;
                cout << "---RECIEVER : " << recieverThread.joinable() << " IN RANK : " << nodeRank << endl;
            }

            if(nodeRank != 0){
                statusThread.join();
                recieverThread.join();
            }

            delete[] localFlag;

            cout << "RANK : " << nodeRank << " ENDED MAIN LOOP" << endl;
        }

        /**
         * @brief Construct a new Matchmaker object
         * The constructor intializes the following variables:
         *  - Child number -> Passed as parameter
         *  - Child ranks -> Passed as parameter
         *  - Tag array -> is instatiated as a new int of child number dimension and all values are set to stable
         *  - Value array -> is instatiated as a new int of child number dimension and all values are set to total particles / child number
         *  - lastVictimRank -> set as -1
         *  - lastTargetRank -> set as -1
         *  - offset -> set as first child rank
         *  - lowerAverage -> set as total particles / child number
         *  - lowerThreshold -> set as a percentage of lower average (25% for the time being)
         * 
         * @param parentRank Rank of parent node
         * @param chunkSize Size of chunk
         * @param recvBuffer Initial reciever buffer
         * @param totalParticles Total particles of node
         * @param localAverage Local average of node
         * @param localThreshold Local threshold of node
         * @param childNumber Number of childs
         * @param childRanks Array containing child ranks
         */
        Matchmaker(int parentRank, int chunkSize, int *recvBuffer, int totalParticles, float localAverage, int localThreshold, int thresholdValue,
                   int childNumber, int *childRanks) : Node(parentRank, chunkSize, recvBuffer, totalParticles,
                   localAverage, localThreshold) {

                   this->childNumber = childNumber;
                   this->childRanks = childRanks;
                   this->tagArray = new int[childNumber];
                   this->valueArray = new int[childNumber];

                   for(int i = 0;i< childNumber;i++){
                        tagArray[i] = STABLE;
                        valueArray[i] = totalParticles/childNumber;
                   }

                   lastVictimRank = -1;
                   lastTargetRank = -1;
                   offset = childRanks[0];
                   lowerAverage = totalParticles/childNumber;
                   lowerThreshold = (lowerAverage * thresholdValue)/100;
        }

        /**
         * @brief Destroy the Matchmaker object
         * The following objects are deleted here : 
         *  - childRanks
         *  - tagArray
         *  - valueArray 
         */
        virtual ~Matchmaker(){
            delete []childRanks;
            delete []tagArray;
            delete []valueArray;
        }
};