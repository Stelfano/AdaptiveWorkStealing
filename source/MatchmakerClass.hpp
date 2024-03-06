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

        MPI_Status waitControlMessages(int *recvFlag){
            MPI_Status stat;

            MPI_Recv(recvFlag, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &stat);
            return stat;
        }

        int findPossibleVictim(int victim){
            
            for(int i = 0;i < childNumber; i++){
                if(tagArray[i] == OVERWORK && i != victim)
                    return i+1;
            }

            return -1;
        }

        int findPossibleTarget(int target){
            for(int i = 0;i < childNumber; i++){
                if(tagArray[i] == IDLE && i != target)
                    return i+1;
            }

            for(int i = 0;i < childNumber; i++){
                if(tagArray[i] == UNDERWORK && i != target)
                    return i+1;
            }

            return -1;
        }

        int checkActiveProcesses(){
            int counter = 0;
            for(int i = 0;i<childNumber;i++){
                if(valueArray[i] != 0){
                    counter++;
                }
            }

            return counter;
        }

        virtual void updateAverage(){
            float localSum = 0;

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

        int stealFromVictim(int *stealingQuantity, int victimRank){
            int actualSteal = 0;

            if(*stealingQuantity > MAX_STEAL){
                *stealingQuantity = MAX_STEAL;
            }
            memset(outWindowBuffer, 0, MAX_STEAL * sizeof(int));

            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, victimRank, 0, outWindow);
            MPI_Get(outWindowBuffer, *stealingQuantity, MPI_INT, victimRank, sizeof(int), *stealingQuantity, MPI_INT, outWindow);
            MPI_Win_unlock(victimRank, outWindow);

            cout << "SAMPLE --> " << outWindowBuffer[0] << endl;


            for(int i = 0;i<*stealingQuantity;i++){
                if(outWindowBuffer[i] == 1 && actualSteal < *stealingQuantity){
                    actualSteal++;
                }
            }

            MPI_Send(&actualSteal, 1, MPI_INT, victimRank, VICTIM, MPI_COMM_WORLD);

            tagArray[victimRank - offset] = LOCKED;

            return actualSteal;
        }

        void sendToTarget(int *stealingQuantity, int targetRank){

            if(*stealingQuantity > MAX_STEAL){
                *stealingQuantity = MAX_STEAL;
            }

            MPI_Win_lock(MPI_LOCK_SHARED, targetRank, 0, inWindow);
            MPI_Put(inWindowBuffer, *stealingQuantity, MPI_INT, targetRank, sizeof(int), *stealingQuantity, MPI_INT, inWindow);

            MPI_Send(stealingQuantity, 1, MPI_INT, targetRank, TARGET, MPI_COMM_WORLD);
            tagArray[targetRank - offset] = LOCKED;
            memset(inWindowBuffer, 0, MAX_STEAL * sizeof(int));

            MPI_Win_unlock(targetRank, inWindow);
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

            int localFlag = 0;						//Conterrà il valore della media locale del singolo nodo
            MPI_Status stat;					//Status della richiesta, ogni comunicazione con un nodo viene salvata qui in modo da poter accedere all
                                                //info di quello specifico nodo, regola chi ha mandato la richiesta e l'aggiornamento del suo status
            int idlePenalty = lowerThreshold / 2;
            int stealingCounter = 0;
            MPI_Request req;

            thread statusThread;
            thread recieverThread;

            if(nodeRank != 0){
                statusThread = activateStatusThread(mainOut);
                recieverThread = activateRecieverThread(mainOut);
            }

            while(true){
                //Given by upper level nodes (except node 0)
                if(checkTermination())
                    break;

                if(nodeRank == 1)
                    printMetrics(mainOut);

                //La recv è bloccante e deve essere soddisfatta si deve usare una IRECV e la richiesta deve poter essere cancellata da uno dei due thread
                if(checkActiveProcesses() > 0){
                    stat = waitControlMessages(&localFlag);	//Si riceve nella variabile local_flag il valore sulla media locale del singolo nodo

                    if(stat.MPI_TAG == OVERWORK && tagArray[stat.MPI_SOURCE-offset] != LOCKED){
                        calculate_time(mainOut);
                        mainOut << "CORE : " << stat.MPI_SOURCE << " IS OVERWORKED WITH : " << localFlag << " PARTICLES" << endl;
                        tagArray[stat.MPI_SOURCE - offset] = stat.MPI_TAG;		//Aggiorno il tag di status del nodo worker
                        valueArray[stat.MPI_SOURCE - offset] = localFlag;		//Aggiorno il valore di elementi del nodo worker
                        updateAverage();
                        notifyAverage(stat.MPI_SOURCE);
                        int target = findPossibleTarget(stat.MPI_SOURCE - offset);

                        if(target != -1){
                            stealingCounter++;
                            //Entrambi meno uno perche si converte da rank a indice del vettore dei valori
                            int quantity = setStealingQuantity(target - offset, stat.MPI_SOURCE - offset);
                            calculate_time(mainOut);
                            mainOut << "CORE : " << stat.MPI_SOURCE << " SHOULD LET " << quantity << " OBJECTS BE STOLEN BY : " << target << endl;
                            int actualStolen = stealFromVictim(&quantity, stat.MPI_SOURCE);
                            calculate_time(mainOut);
                            mainOut << "ACTUAL STOLEN : " << actualStolen << endl;
                            sendToTarget(&actualStolen, target);
                            checkForDoubleSteal(stat.MPI_SOURCE, target);
                            calculate_time(mainOut);
                        }

                        mainOut.emit();
                    }

                    if(stat.MPI_TAG == STABLE && tagArray[stat.MPI_SOURCE-offset] != LOCKED){
                        calculate_time(mainOut);
                        mainOut << "CORE : " << stat.MPI_SOURCE << " IS STABLE WITH " << localFlag << " PARTICLES" << endl;
                        tagArray[stat.MPI_SOURCE - offset] = stat.MPI_TAG;
                        valueArray[stat.MPI_SOURCE - offset] = localFlag;
                        updateAverage();
                        notifyAverage(stat.MPI_SOURCE);
                        
                        mainOut.emit();
                    }

                    if(stat.MPI_TAG == UNDERWORK && tagArray[stat.MPI_SOURCE - offset] != LOCKED){
                        calculate_time(mainOut);
                        mainOut << "CORE : " << stat.MPI_SOURCE << " IS UNDERWORKED WITH " << localFlag << " PARTICLES" << endl;
                        tagArray[stat.MPI_SOURCE - offset] = stat.MPI_TAG;
                        valueArray[stat.MPI_SOURCE - offset] = localFlag;
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
                            checkForDoubleSteal(victim, stat.MPI_SOURCE-offset);
                            calculate_time(mainOut);
                        }

                        mainOut.emit();
                    }

                    if(stat.MPI_TAG == IDLE && tagArray[stat.MPI_SOURCE-offset] != LOCKED){
                        calculate_time(mainOut);
                        mainOut << "CORE : " << stat.MPI_SOURCE << " IS IDLE WITH : " << localFlag << " PARTICLES" << endl;
                        tagArray[stat.MPI_SOURCE - offset] = stat.MPI_TAG;
                        valueArray[stat.MPI_SOURCE - offset] = localFlag;
                        calculate_time(mainOut);

                        if(lowerThreshold/2 > MIN_THRESHOLD){
                            lowerThreshold = lowerThreshold/2; 		//Penalità per core IDLE
                            notifyThreshold();
                        }

                        calculate_time(mainOut);
                        mainOut << "TRESHOLD DECREASES TO : " << lowerThreshold << endl;
                        updateAverage();
                        calculate_time(mainOut);
                        mainOut << "A WORKER HAS GONE IDLE! NOTIFY AVERAGE TO ALL WORKERS " << endl;
                        for(int i = 0;i<childNumber;i++){
                            notifyAverage(i);
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
                            sendToTarget(&actualStolen, stat.MPI_SOURCE);
                            checkForDoubleSteal(victim, stat.MPI_SOURCE-offset);
                            calculate_time(mainOut);
                        }

                        mainOut.emit();
                    }

                    if(stat.MPI_TAG == UNLOCKED){
                        calculate_time(mainOut);
                        mainOut << "CORE : " << stat.MPI_SOURCE << " IS NOW AVAILABLE FOR STEALING " << endl;
                        tagArray[stat.MPI_SOURCE - offset] = UNLOCKED;
                        valueArray[stat.MPI_SOURCE - offset] = localFlag;
                        updateAverage();
                        notifyAverage(stat.MPI_SOURCE);

                        mainOut.emit();
                    }
                }
            }		

            calculate_time(mainOut);
            mainOut << "MATCHMAKER WITH RANK : " << nodeRank << " INITIATING ENDING PROCEDURES..." << endl;
            mainOut.emit();

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
                        valueArray[i] = chunkSize;
                   }

                   lastVictimRank = -1;
                   lastTargetRank = -1;
                   offset = childRanks[0];
                   //Attenzione a queste
                   lowerAverage = chunkSize;
                   lowerThreshold = (chunkSize * 10)/100;
        }

        virtual ~Matchmaker(){
            delete []childRanks;
            delete []tagArray;
            delete []valueArray;
        }
};