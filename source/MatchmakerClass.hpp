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

    int childNumber;
    int *childRanks;
    int *tagArray;
    int *valueArray;
    int lastVictimRank;
    int lastTargetRank;
    int offset;
    float lowerAverage;
    int lowerThreshold;
    int *childFlag;

        MPI_Status waitControlMessages(int *recvFlag){
            MPI_Status stat;

            MPI_Recv(recvFlag, 2, MPI_INT, MPI_ANY_SOURCE, UPDATE, MPI_COMM_WORLD, &stat);
            return stat;
        }

        MPI_Status waitControlMessages(int *recvFlag, MPI_Request *req){
            MPI_Status stat;

            while(true){
                for(int i=0;i<childNumber;i++){
                    MPI_Test(req+i, childFlag+i, &stat);
                    if(childFlag[i] == true){
                        childFlag[i] == false;
                        MPI_Irecv(recvFlag, 1, MPI_INT, i+offset, UPDATE, MPI_COMM_WORLD, req+i);
                        cout << "RANK : " << nodeRank << " RECIEVED MESSAGE FROM : " << stat.MPI_SOURCE << endl;
                        MPI_Recv(recvFlag, 1, MPI_INT, i+offset, MPI_ANY_TAG, MPI_COMM_WORLD, &stat);
                        cout << "RANK : " << stat.MPI_SOURCE << " IS : " << stat.MPI_TAG << endl;
                        return stat;
                    }
                }
            }
            
        }

        int findPossibleVictim(int victim){
            
            for(int i = 0;i < childNumber; i++){
                if(tagArray[i] == OVERWORK && i != victim)
                    return i+offset;
            }

            return -1;
        }

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

        int checkActiveProcesses(){
            int counter = 0;
            for(int i = 0;i<childNumber;i++){
                if(valueArray[i] != 0 || tagArray[i] == LOCKED){
                    counter++;
                }
            }

            return counter;
        }

        bool anyNodeIsLocked(){
            for(int i=0;i<childNumber;i++){
                if(tagArray[i] == LOCKED){
                    return true;
                }
            }

            return false;
        }

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

        void updateValues(int source, int *localFlag){
            tagArray[source - offset] = localFlag[1];
            valueArray[source - offset] = localFlag[0];
        }

        void notifyAverage(int reciever){
            MPI_Request req;
            
            MPI_Send(&(this->lowerAverage), 1, MPI_FLOAT, reciever, AVERAGE, MPI_COMM_WORLD);
        }

        void notifyThreshold(){

            for(int i = 0;i<childNumber;i++){
                MPI_Send(&(this->lowerThreshold), 1, MPI_INT, childRanks[i], THRESHOLD, MPI_COMM_WORLD);
            }
        }

        void checkForDoubleSteal(int currentVictimRank, int currentTargetRank){
            if((currentVictimRank == lastVictimRank && currentTargetRank == lastTargetRank) || (currentVictimRank == lastTargetRank && currentTargetRank == lastVictimRank)){
                lowerThreshold *= 2;
                notifyThreshold();
            }

            lastVictimRank = currentVictimRank;
            lastTargetRank = currentTargetRank;
        }

        int setStealingQuantity(int targetIndex, int victimIndex){

            int targetDistance = abs(lowerAverage - valueArray[targetIndex]);
            int victimDistance = abs(lowerAverage - valueArray[victimIndex]);

            return ((targetDistance + victimDistance)/2);
        }

        void printMetrics(osyncstream &mainOut){
            mainOut << "MATCHMAKER " << nodeRank << " DECLARES METRICS " << endl << "\t THRESHOLD : " << lowerThreshold << endl << "\t AVERAGE : " << lowerAverage << endl << "\tTOTAL PARTICLES : " << totalParticles << endl;
            mainOut << "\t VALUES : " << endl;
            for(int i = 0;i < childNumber;i++){
                mainOut << "\t\tRANK : " << i+offset << " -> " << valueArray[i] << " -- " << tagArray[i] << endl;
            }

            mainOut << endl;
            mainOut.emit();
        }

        virtual void deleteDataFromNode(int stealingQuantity){
            cout << "DELETING : " << stealingQuantity << " FROM NODE : " << nodeRank << endl; 
            int actualSteal = gatherData(stealingQuantity);

            cout << "RANK : " << nodeRank << " SENDING ALL CLEAR TO PARENT..." << endl;
            MPI_Send(&actualSteal, 1, MPI_INT, parentRank, COMM, MPI_COMM_WORLD);
            cout << "SENT : " << actualSteal << " PARTICLES UPWARDS" << endl;
            while(anyNodeIsLocked()){}
            declareStatus(UNLOCKED);
        }

        virtual void injectDataInNode(int stealingQuantity){
            cout << "INJECTING : " << stealingQuantity << " IN NODE : " << nodeRank << endl;
            distributeData(stealingQuantity);

            cout << "SENT DATA TO LOWER NODES" << endl;
            while(anyNodeIsLocked()){}
            declareStatus(UNLOCKED);
        }


        virtual void distributeData(int stealingQuantity){
            int *stealingArray = calculateStealing(stealingQuantity, true);
            int arrayOffset = 0;
            int temp = 0;

            for(int i=0;i<childNumber;i++){
                tagArray[i] = LOCKED;
                cout << "DISTRIBUTING : " << stealingArray[i] << " PARTICLES TO : " << childRanks[i] << endl;
                MPI_Win_lock(MPI_LOCK_EXCLUSIVE, childRanks[i], 0, inWindow);
                MPI_Put(inWindowBuffer+arrayOffset, stealingArray[i], MPI_INT, childRanks[i], sizeof(int), stealingQuantity+i, MPI_INT, inWindow);
                MPI_Win_unlock(childRanks[i], inWindow);

                arrayOffset += stealingArray[i];

                MPI_Send(stealingArray+i, 1, MPI_INT, childRanks[i], TARGET, MPI_COMM_WORLD);
            }
            totalParticles+=stealingQuantity;

            delete[] stealingArray;
        }
        
        int *calculateStealing(int stealingQuantity, bool evenDistribute = false){
            float *percentages = new float[childNumber];
            int *stealingValues = new int[childNumber];

            for(int i=0;i<childNumber;i++){
                if(!evenDistribute)
                    percentages[i] = ((float)valueArray[i]/totalParticles);
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

        virtual int gatherData(int stealingQuantity){
            int *stealingArray = calculateStealing(stealingQuantity);
            int *tempArray = new int[stealingQuantity];
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
            delete[] tempArray;
            delete[] stealingArray;
            return completeSteal;
        }

        //Stealing tra matchmaker
        virtual int stealFromVictim(int *stealingQuantity, int victimRank){
            int actualSteal = 0;

            tagArray[victimRank - offset] = LOCKED;
            cout << "NODE RANK : " << nodeRank << " IS STEALING FROM BRANCH WITH ROOT : " << victimRank << endl;
            MPI_Send(stealingQuantity, 1, MPI_INT, victimRank, VICTIM, MPI_COMM_WORLD);
            MPI_Recv(&actualSteal, 1, MPI_INT, victimRank, COMM, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            cout << "NODE RANK : " << nodeRank << " RECIEVED ALL CLEAR TO STEAL : " << actualSteal << " PARTICLES" << endl;

            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, victimRank, 0, outWindow);
            MPI_Get(outWindowBuffer, actualSteal, MPI_INT, victimRank, sizeof(int), actualSteal, MPI_INT, outWindow);
            MPI_Win_unlock(victimRank, outWindow);

            cout << "ROOT OF STEALING HAS STOLEN : " << actualSteal << " PARTICLES" << endl;
            memcpy(inWindowBuffer, outWindowBuffer, actualSteal);
            return actualSteal;
        }

        virtual void sendToTarget(int *stealingQuantity, int targetRank){
            if(*stealingQuantity == 0){
                cout << "WILL NOT SEND DATA TO TARGET" << endl;
                return;
            }

            tagArray[targetRank - offset] = LOCKED;

            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, targetRank, 0, inWindow);
            MPI_Put(inWindowBuffer, *stealingQuantity, MPI_INT, targetRank, sizeof(int), *stealingQuantity, MPI_INT, inWindow);
            MPI_Win_unlock(targetRank, inWindow);

            MPI_Send(stealingQuantity, 1, MPI_INT, targetRank, TARGET, MPI_COMM_WORLD);
        }

        virtual int checkTermination(){
            return done;
        }

        virtual thread activateStatusThread(osyncstream &mainOut){
            return thread(&Node::sendStatusFunction, this, ref(mainOut));
        }

        virtual thread activateRecieverThread(osyncstream &mainOut){
            return thread(&Node::recieveMessageFromMatchmaker, this, ref(mainOut));
        }

        virtual void endingProcedure(){
            cout << "RANK : " << nodeRank << " HAS ENDED COMPUTATION... " << endl;
        }


    public:

        void matchmakerMainLoop(int * globalResult, osyncstream &mainOut){

            int *localFlag = new int[2];		//Conterrà il valore della media locale del singolo nodo
            MPI_Status stat;					//Status della richiesta, ogni comunicazione con un nodo viene salvata qui in modo da poter accedere all
                                                //info di quello specifico nodo, regola chi ha mandato la richiesta e l'aggiornamento del suo status
            int idlePenalty = lowerThreshold / 2;
            int stealingCounter = 0;

            thread statusThread;
            thread recieverThread;

            if(nodeRank != 0){
                statusThread = activateStatusThread(mainOut);
                recieverThread = activateRecieverThread(mainOut);
            }

            while(true){
                if(checkTermination())
                    break;

                if(checkActiveProcesses() > 0){

                    printMetrics(mainOut);
                    stat = waitControlMessages(localFlag);


                    if(localFlag[1] == LOCKED){
                        tagArray[stat.MPI_SOURCE - offset] = LOCKED;
                        calculate_time(mainOut);
                        mainOut << "NODE RANK : " << nodeRank << "RECIEVES MESSAGE FROM : " << stat.MPI_SOURCE << " OF LOCKING" << endl;
                        mainOut.emit();
                    }

                    if(localFlag[1] == OVERWORK && tagArray[stat.MPI_SOURCE-offset] != LOCKED){
                        calculate_time(mainOut);
                        mainOut << nodeRank << " CORE : " << stat.MPI_SOURCE << " IS OVERWORKED WITH : " << localFlag[0] << " PARTICLES" << endl;
                        updateValues(stat.MPI_SOURCE, localFlag);
                        updateAverage();
                        notifyAverage(stat.MPI_SOURCE);
                        int target = findPossibleTarget(stat.MPI_SOURCE - offset);

                        if(target != -1){
                            stealingCounter++;
                            //Entrambi meno uno perche si converte da rank a indice del vettore dei valori
                            int quantity = setStealingQuantity(target - offset, stat.MPI_SOURCE - offset);
                            calculate_time(mainOut);
                            mainOut << "CORE : " << stat.MPI_SOURCE << " SHOULD LET " << quantity << " OBJECTS BE STOLEN BY : " << target << endl;
                            mainOut.emit();
                            int actualStolen = stealFromVictim(&quantity, stat.MPI_SOURCE);
                            calculate_time(mainOut);
                            mainOut << "ACTUAL STOLEN : " << actualStolen << endl;
                            mainOut.emit();
                            sendToTarget(&actualStolen, target);
                            //checkForDoubleSteal(stat.MPI_SOURCE, target);
                            calculate_time(mainOut);
                        }

                        mainOut.emit();
                    }

                    if(localFlag[1] == STABLE && tagArray[stat.MPI_SOURCE-offset] != LOCKED){
                        calculate_time(mainOut);
                        mainOut << "CORE : " << stat.MPI_SOURCE << " IS STABLE WITH " << localFlag[0] << " PARTICLES" << endl;
                        updateValues(stat.MPI_SOURCE, localFlag);
                        updateAverage();
                        notifyAverage(stat.MPI_SOURCE);
                        
                        mainOut.emit();
                    }

                    if(localFlag[1] == UNDERWORK && tagArray[stat.MPI_SOURCE - offset] != LOCKED){
                        calculate_time(mainOut);
                        mainOut << "CORE : " << stat.MPI_SOURCE << " IS UNDERWORKED WITH " << localFlag[0] << " PARTICLES" << endl;
                        updateValues(stat.MPI_SOURCE, localFlag);
                        updateAverage();
                        notifyAverage(stat.MPI_SOURCE);
                        int victim = findPossibleVictim(stat.MPI_SOURCE - offset);

                        if(victim != -1){
                            stealingCounter++;
                            int quantity = setStealingQuantity(stat.MPI_SOURCE-offset, victim-offset);
                            calculate_time(mainOut);
                            mainOut << "CORE : " << stat.MPI_SOURCE << " SHOULD STEAL FROM " << victim << " " << quantity << " OBJECTS" << endl;
                            int actualStolen = stealFromVictim(&quantity, victim);
                            calculate_time(mainOut);
                            mainOut << "ACTUAL STOLEN : " << actualStolen << " FROM RANK : " << victim << endl;
                            sendToTarget(&actualStolen, stat.MPI_SOURCE);
                            //checkForDoubleSteal(victim, stat.MPI_SOURCE);
                            calculate_time(mainOut);
                        }

                        mainOut.emit();
                    }

                    if(localFlag[1] == IDLE && tagArray[stat.MPI_SOURCE-offset] != LOCKED){
                        calculate_time(mainOut);
                        mainOut << "CORE : " << stat.MPI_SOURCE << " IS IDLE WITH : " << localFlag[0] << " PARTICLES" << endl;
                        updateValues(stat.MPI_SOURCE, localFlag);
                        calculate_time(mainOut);

                        if(lowerThreshold/2 > MIN_THRESHOLD){
                            lowerThreshold = lowerThreshold/2;
                            notifyThreshold();
                        }

                        calculate_time(mainOut);
                        mainOut << "TRESHOLD DECREASES TO : " << lowerThreshold << endl;
                        updateAverage();
                        calculate_time(mainOut);
                        mainOut << "A WORKER HAS GONE IDLE! NOTIFY AVERAGE TO ALL WORKERS " << endl;
                        for(int i = 0;i<childNumber;i++){
                            calculate_time(mainOut);
                            mainOut << "SENDING AVERAGE OF : " << lowerAverage << " TO : " << i + offset << endl;
                            notifyAverage(i+offset);
                        }

                        int victim = findPossibleVictim(stat.MPI_SOURCE - offset);

                        if(victim != -1){
                            stealingCounter++;
                            int quantity = setStealingQuantity(stat.MPI_SOURCE-offset, victim-offset);
                            calculate_time(mainOut);
                            mainOut << "CORE : " << stat.MPI_SOURCE << " SHOULD IMMEDIATLY STEAL FROM " << victim << " " << quantity << " OBJECTS" << endl;
                            int actualStolen = stealFromVictim(&quantity, victim);
                            calculate_time(mainOut);
                            mainOut << "ACTUAL STOLEN : " << actualStolen << " FROM RANK : " << victim << endl;
                            mainOut.emit();
                            sendToTarget(&actualStolen, stat.MPI_SOURCE);
                            //checkForDoubleSteal(victim, stat.MPI_SOURCE);
                            calculate_time(mainOut);
                        }

                        mainOut.emit();
                    }

                    if(localFlag[1] == UNLOCKED){
                        calculate_time(mainOut);
                        mainOut << "-------CORE : " << stat.MPI_SOURCE << " IS NOW AVAILABLE FOR STEALING " << endl;
                        updateValues(stat.MPI_SOURCE, localFlag);

                        for(int i = 0;i<childNumber;i++){
                            if(tagArray[i] != LOCKED){
                                updateAverage();
                                notifyAverage(stat.MPI_SOURCE);
                            }
                        }

                        mainOut.emit();
                    }
                }
            }		

            calculate_time(mainOut);
            mainOut << "MATCHMAKER WITH RANK : " << nodeRank << " INITIATING ENDING PROCEDURES..." << endl;
            mainOut.emit();

            printMetrics(mainOut);

            lowerThreshold = 0;
            lowerAverage = 0;
            for(int i = 0;i<childNumber; i++){
                notifyAverage(childRanks[i]);
            }

            endingProcedure();

            if(nodeRank != 0){
                statusThread.join();
                recieverThread.join();
            }

            mainOut << "RANK : " << nodeRank << " ENDED MAIN LOOP" << endl;
            mainOut.emit();
        }

        Matchmaker(int parentRank, int chunkSize, int *recvBuffer, int totalParticles, float localAverage, int localThreshold,
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
                   lowerThreshold = (lowerAverage * 25)/100;
                   childFlag = new int[childNumber];
                   memset(childFlag, 0, sizeof(childFlag));
        }

        virtual ~Matchmaker(){
            delete []childRanks;
            delete []tagArray;
            delete []valueArray;
            delete []childFlag;
        }
};