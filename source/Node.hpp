#ifndef NODE
#define NODE
#include "utils.hpp"
#include "mpi.h"
#include <vector>
#include <random>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <cstring> 
#include <shared_mutex>
#include <mutex>
#include <syncstream>


using namespace std;

class Node{

    protected:
        int treeWidth;
        int nodeRank;
        int parentRank;
        int *inWindowBuffer;
        int *outWindowBuffer;
        int *recvBuffer;
        int chunkSize;
        int status;
        int totalParticles;
        float localAverage;
        int localThreshold;
        MPI_Win inWindow;
        MPI_Win outWindow;
        atomic<bool> done;
        bool *sentFlag;
        mutable shared_mutex totalParticleMutex;
        mutable shared_mutex sentFlagMutex;

        //Non c'è veramente bisogno di usare una funzione
        void declareStatus(int status){
            this->status = status;
            int *temp = new int[2];
            temp[0] = totalParticles;
            temp[1] = status;
            MPI_Send(temp, 2, MPI_INT, parentRank, UPDATE, MPI_COMM_WORLD);
            delete[] temp;
        }

        int returnTotalParticles(){
            return this->totalParticles;
        }

    public:

        virtual void sendStatusFunction(std::osyncstream &senderOut){
            shared_lock<shared_mutex> totalParticlesLock(totalParticleMutex, defer_lock);
            unique_lock<shared_mutex> sentFlagLock(sentFlagMutex, defer_lock);

            while(!done){	

                if(status != LOCKED){
                    totalParticlesLock.lock();
                    if(totalParticles > (localAverage + localThreshold) && sentFlag[3] == false && totalParticles != 0){
                        declareStatus(OVERWORK);
                        sentFlagLock.lock();
                        sentFlag[3] = true;

                        sentFlag[2] = false;
                        sentFlag[1] = false;
                        sentFlag[0] = false;
                        sentFlagLock.unlock();
                    }
                    

                    if(totalParticles <= (localAverage + localThreshold) && totalParticles >= (localAverage - localThreshold) && sentFlag[2] == false && totalParticles != 0){
                        declareStatus(STABLE);
                        sentFlagLock.lock();
                        sentFlag[2] = true;

                        sentFlag[3] = false;
                        sentFlag[1] = false;
                        sentFlag[0] = false;
                        sentFlagLock.unlock();
                    }


                    if(totalParticles < (localAverage - localThreshold) && sentFlag[1] == false && totalParticles != 0){
                        declareStatus(UNDERWORK);
                        if(nodeRank == 2){
                            calculate_time(senderOut);
                            senderOut << localAverage << " " << localThreshold << endl;
                            senderOut.emit();
                        }
                        sentFlagLock.lock();
                        sentFlag[1] = true;

                        sentFlag[3] = false;
                        sentFlag[2] = false;
                        sentFlag[0] = false;
                        sentFlagLock.unlock();
                    }


                    if(totalParticles == 0 && sentFlag[0] == false){
                        totalParticles = 0;
                        declareStatus(IDLE);
                        sentFlagLock.lock();
                        sentFlag[0] = true;

                        sentFlag[3] = false;
                        sentFlag[2] = false;
                        sentFlag[1] = false;
                        sentFlagLock.unlock();
                    }

                    totalParticlesLock.unlock();
                }
            }

            calculate_time(senderOut);
            senderOut << "SENDER THREAD TERMINATING IN RANK : "<< nodeRank << endl;
            senderOut.emit();
        }

        void recieveMessageFromMatchmaker(osyncstream &recieverOut){
            MPI_Status stat1, stat2, stat3, stat4;
            MPI_Request req1, req2, req3, req4;
            int flag1 = false;
            int flag2 = false;
            int flag3 = false;
            int flag4 = false;
            float bufferAvg = this->localAverage;
            int bufferThreshold = this->localThreshold;
            int stealingBuffer = 0;

            unique_lock<shared_mutex> totalParticlesLock(totalParticleMutex, defer_lock);
            unique_lock<shared_mutex> sentFlagLock(sentFlagMutex, defer_lock);

            calculate_time(recieverOut);
            recieverOut << "STARTING RECIEVER THREAD IN RANK : " << nodeRank << endl;
            recieverOut.emit();

            MPI_Irecv(&bufferAvg, 1, MPI_FLOAT, parentRank, AVERAGE, MPI_COMM_WORLD, &req1);
            MPI_Irecv(&bufferThreshold, 1, MPI_INT, parentRank, THRESHOLD, MPI_COMM_WORLD, &req2);
            MPI_Irecv(&stealingBuffer, 1, MPI_INT, parentRank, VICTIM, MPI_COMM_WORLD, &req3);
            MPI_Irecv(&stealingBuffer, 1, MPI_INT, parentRank, TARGET, MPI_COMM_WORLD, &req4);

            while(bufferAvg > 0){
                MPI_Test(&req1, &flag1, &stat1);
                MPI_Test(&req2, &flag2, &stat2);
                MPI_Test(&req3, &flag3, &stat3);
                MPI_Test(&req4, &flag4, &stat4);

                if(flag1 == true){
                    MPI_Irecv(&bufferAvg, 1, MPI_FLOAT, parentRank, AVERAGE, MPI_COMM_WORLD, &req1);
                    localAverage = bufferAvg;
                    if(nodeRank == 2)
                        recieverOut << "-----RECIEVED AVG OF : " << bufferAvg << endl;

                    recieverOut.emit();
                    flag1 = false;
                }

                if(flag2 == true){
                    MPI_Irecv(&bufferThreshold, 1, MPI_INT, parentRank, THRESHOLD, MPI_COMM_WORLD, &req2);
                    localThreshold = bufferThreshold;
                    flag2 = false;
                }

                if(flag3 == true){
                    status = LOCKED;
                    MPI_Irecv(&stealingBuffer, 1, MPI_INT, parentRank, VICTIM, MPI_COMM_WORLD, &req3);
                    
                    totalParticlesLock.lock();
                    deleteDataFromNode(stealingBuffer);
                    totalParticlesLock.unlock();

                    sentFlagLock.lock();
                    memset(sentFlag, 0, sizeof(bool) * 4);
                    sentFlagLock.unlock();

                    flag3 = false;	
                }

                if(flag4 == true){
                    recieverOut << " NODE : " << nodeRank << " RECIEVED INJECTION OF : " << stealingBuffer << " PARTICLES" << endl;
                    status = LOCKED;
                    MPI_Irecv(&stealingBuffer, 1, MPI_INT, parentRank, TARGET, MPI_COMM_WORLD, &req4);
                    calculate_time(recieverOut);
                    recieverOut.emit();

                    totalParticlesLock.lock();
                    injectDataInNode(stealingBuffer);
                    totalParticlesLock.unlock();

                    sentFlagLock.lock();
                    memset(sentFlag, 0, sizeof(bool) * 4);
                    sentFlagLock.unlock();

                    flag4 = false;	
                }
            }

            done = true;
            MPI_Cancel(&req2);
            MPI_Cancel(&req3);
            MPI_Cancel(&req4);
            calculate_time(recieverOut);
            recieverOut << "CLOSING RECIEVE THREAD IN RANK : " << nodeRank << endl;
            recieverOut.emit();
        }

        virtual void injectDataInNode(int stealingQuantity){
            totalParticles += stealingQuantity;

            declareStatus(UNLOCKED);
        }

        virtual void deleteDataFromNode(int stealingQuantity){
            totalParticles -= stealingQuantity;
 
            declareStatus(UNLOCKED);
        }


        Node(int parentRank, int chunkSize, int *recvBuffer, int totalParticles, float localAverage, int localThreshold){
            MPI_Comm_rank(MPI_COMM_WORLD, &(this->nodeRank));
            this->parentRank = parentRank;
            this->chunkSize = chunkSize;
            inWindowBuffer = new int[MAX_STEAL];
            outWindowBuffer = new int[MAX_STEAL];
            this->recvBuffer = recvBuffer;
            status = STABLE;
            this->totalParticles = totalParticles;
            this->localAverage = localAverage;
            this->localThreshold = localThreshold;
            sentFlag = new bool[4];
            sentFlag[0] = 0;
            sentFlag[1] = 0;
            sentFlag[2] = 1;
            sentFlag[3] = 0;
            done = false;
            
            MPI_Win_create(inWindowBuffer, sizeof(int) * MAX_STEAL, sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &(this->inWindow));
            MPI_Win_create(outWindowBuffer, sizeof(int) * MAX_STEAL, sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &(this->outWindow));
        }

        virtual ~Node(){
            delete []recvBuffer;
            delete []outWindowBuffer;
            delete []inWindowBuffer;
            delete []sentFlag;

            cout << "DELETION SUCCESSFUL OF NODE : " << nodeRank << endl; 
        }
};

#endif