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

/**
 * @file Node.hpp
 * @author Stefano Romeo
 * @brief Definition for Node class
 * @version 0.1
 * @date 2024-04-04
 * 
 */


using namespace std;
/**
 * @brief Node class is the base class from which every node specialization comes from, this class implements all base functions for communication and other routines shared across all 
 *        elements in communication
 * 
 *This class implements basic recieve and send update functions necessary to the correct execution of the program, it is a clean slate necessary to specialize more specific roles inside
 *the overall system, this class is not abstract nor an interface but it's still not intended to be instanciated
 */
class Node{

    protected:
        int treeWidth;                                      ///<Width of the computational tree
        int nodeRank;                                       ///<Rank of node
        int parentRank;                                     ///<Rank of parent (if the current node is 0 parent rank is -1)
        int *inWindowBuffer;                                ///<Pointer to input window buffer
        int *outWindowBuffer;                               ///<Pointer to output window buffer
        int *recvBuffer;                                    ///<Pointer to initial recieve buffer
        int chunkSize;                                      ///<Number of particle sent in a single chunk
        int status;                                         ///<Status of node
        int totalParticles;                                 ///<Total particle present in the node
        float localAverage;                                 ///<Current average of node
        int localThreshold;                                 ///<Current threshold of node
        int thresholdValue;                                 ///<Value of threshold
        MPI_Win inWindow;                                   ///<MPI object to use RDMA for input window buffer
        MPI_Win outWindow;                                  ///<MPI object to use RDMA for output window buffer
        atomic<bool> done;                                  ///<Atomic variable to signal termination
        bool *sentFlag;                                     ///<Use flag to send data upwards, prevents multiple update of the same status
        mutable shared_mutex totalParticleMutex;            ///<Shared mutex to prevent race condition on Total particles and Particle buffer(Only in worker)
        mutable shared_mutex sentFlagMutex;                 ///<Shared mutex to prevent race condition on status update flag array

        
        /**
         * @brief Status declaration function, status is updated and then communicated to parent rank in the hierarchy
         * 
         * In order to prevent the wrong reception on parent rank (There are many Recv posted at the same time with different tags, a Recv in a matchmaker could Recv the wrong message)
         * status and total particle count are communicated through a vector of ints, firts position of array contains total particle count, second position is an int indicating update of status
         * updates are signaled using the UPDATE tag to prevent the aformentioned issue
         * @param status Status sent to upper node
         */
        void declareStatus(int status){
            this->status = status;
            int *temp = new int[2];
            temp[0] = totalParticles;
            temp[1] = status;
            MPI_Send(temp, 2, MPI_INT, parentRank, UPDATE, MPI_COMM_WORLD);

            delete[] temp;
        }

    public:

        /**
         * @brief Update function for a node, this function is assigned to a thread to be able to update status when necessary
         *
         * This function is responsable to send status update upwards, status update are sent using 4 different states:
         *  - OVERWORK : \f$Total Particles > {\mu} + {\sigma}\f$
         *  - STABLE : mu - sigma <= Total Particles <= mu + sigma
         *  - UNDERWORK : Total Particles < mu - sigma
         *  - IDLE : Total Particles = 0
         * 
         */
        void sendStatusFunction(){
            shared_lock<shared_mutex> totalParticlesLock(totalParticleMutex, defer_lock);
            unique_lock<shared_mutex> sentFlagLock(sentFlagMutex, defer_lock);

            while(!done){	

                if(status != LOCKED){
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
                        sentFlagLock.lock();
                        sentFlag[1] = true;

                        sentFlag[3] = false;
                        sentFlag[2] = false;
                        sentFlag[0] = false;
                        sentFlagLock.unlock();
                    }


                    if(totalParticles == 0 && sentFlag[0] == false){
                        declareStatus(IDLE);
                        sentFlagLock.lock();
                        sentFlag[0] = true;

                        sentFlag[3] = false;
                        sentFlag[2] = false;
                        sentFlag[1] = false;
                        sentFlagLock.unlock();
                    }
                }
            }

            calculate_time();
            cout << "SENDER THREAD TERMINATING IN RANK : "<< nodeRank << endl;
        }

        /**
         * @brief Reciever function to obtain metric updates on average, threshold and recieve work stealing related messages
         *
         * This fuction recieves updates from parent rank matchmaker, when recieving work-stealing messages function are called with respect to the reqeusted operation
         * it is important to note that the thread assigned to this function executes work stealing and passes around particles, this comes with a series of problems and advantages,
         * reduction(if in a worker) is not stopped, the mechanism has to be regulated to not interfiere with other parts of the system
         *   
         * The thread assigned to the function is also the one that sets termination flag to 0 indicating the end of reduction and the beginning of gathering operations, this flag is set
         * to 0 when the parent matchmaker sends an update signaling that the local average is 0, this cancels all recieving calls of MPI and begins ending procedure in the node
         * 
         */
        void recieveMessageFromMatchmaker(){
            MPI_Status stat1, stat3, stat4;
            MPI_Request req1, req3, req4;
            int flag1 = false;
            int flag3 = false;
            int flag4 = false;
            float bufferAvg = this->localAverage;
            int stealingBuffer = 0;

            unique_lock<shared_mutex> totalParticlesLock(totalParticleMutex, defer_lock);
            unique_lock<shared_mutex> sentFlagLock(sentFlagMutex, defer_lock);
            calculate_time();

            MPI_Irecv(&bufferAvg, 1, MPI_FLOAT, parentRank, AVERAGE, MPI_COMM_WORLD, &req1);
            MPI_Irecv(&stealingBuffer, 1, MPI_INT, parentRank, VICTIM, MPI_COMM_WORLD, &req3);
            MPI_Irecv(&stealingBuffer, 1, MPI_INT, parentRank, TARGET, MPI_COMM_WORLD, &req4);

            while(bufferAvg > 0){
                MPI_Test(&req1, &flag1, &stat1);
                MPI_Test(&req3, &flag3, &stat3);
                MPI_Test(&req4, &flag4, &stat4);

                if(flag1 == true){
                    MPI_Irecv(&bufferAvg, 1, MPI_FLOAT, parentRank, AVERAGE, MPI_COMM_WORLD, &req1);
                    cout << "RANK : " << nodeRank << " RECIEVED AVG OF : " << bufferAvg << endl;
                    localAverage = bufferAvg;
                    localThreshold = (localAverage * thresholdValue)/100;
                    if(bufferAvg == 0){
                        cout << "------RECIEVED TERMINATION IN RANK : " << nodeRank << endl;
                    }
                    flag1 = false;
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
                    cout << " NODE : " << nodeRank << " RECIEVED INJECTION OF : " << stealingBuffer << " PARTICLES" << endl;
                    status = LOCKED;
                    MPI_Irecv(&stealingBuffer, 1, MPI_INT, parentRank, TARGET, MPI_COMM_WORLD, &req4);
                    calculate_time();

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
            MPI_Cancel(&req1);
            MPI_Cancel(&req3);
            MPI_Cancel(&req4);
            calculate_time();
            cout << "CLOSING RECIEVE THREAD IN RANK : " << nodeRank << endl;
        }

        /**
         * @brief Basic template of data injection for a node
         *  
         * Particles are injected in a node, this function is overloaded in specialization classes, at the end of the operation status is then declared with a special tag
         * (UNLOCKED) to indicate to parent rank that the node is ready to perform work stealing
         * 
         * @param stealingQuantity Quantity of injected particles in node
         */
        virtual void injectDataInNode(int stealingQuantity){
            totalParticles += stealingQuantity;

            declareStatus(UNLOCKED);
        }

        /**
         * @brief Basic template of data deletion for a node
         *  
         * Particles are deleted from a node, this function is overloaded in specialization classes, at the end of the operation status is then declared with a special tag
         * (UNLOCKED) to indicate to parent rank that the node is ready to perform work stealing
         * 
         * @param stealingQuantity Quantity of injected particles in node
         */
        virtual void deleteDataFromNode(int stealingQuantity){
            totalParticles -= stealingQuantity;
 
            declareStatus(UNLOCKED);
        }


        /**
         * @brief Construct a new Node object
         *
         * Constructor initializes the following elements:
         *  - nodeRank -> Inizialized by calling MPI_Comm_rank function
         *  - parentRank -> Parent rank is passed as one of the parameters
         *  - chunkSize -> Chunk size is passed as one of the parameters
         *  - inWindowBuffer -> Input window buffer is allocated
         *  - outWindowBuffer -> Output window buffer is allocated
         *  - recvBuffer -> Recieve buffer is passed as one of the parameters
         *  - status -> Status is set as STABLE by default as every node has the same number of particles at the beginning of computation
         *  - totalparticles -> Total particles number is passed as one the parameters
         *  - localAverage -> Local Average is passed as one of the parameters
         *  - localThreshold -> Local threshold is passed as one of the parameters
         *  - sentFlag -> Flag array is initialized and STABLE flag is set to 1
         *  - done -> Termination flag is set to false
         *  - inWindow -> inWindow MPI object is initialized with the corrisponding MPI function
         *  - outWindow -> outWindow MPI object is initialized with the corrisponding MPI function
         *
         * @param parentRank Rank of parent
         * @param chunkSize Chunk size, differs with the level at which the node is located
         * @param recvBuffer recvBuffer is passed as a pointer
         * @param totalParticles totalParticles, initial particle assigned to a node, differs with the level of a node
         * @param localAverage initial local Average differs with level and is passed when object is created
         * @param localThreshold initial threshold is set as a percentage of local average (10% - 30% of initial average)
         */

        Node(int parentRank, int chunkSize, int *recvBuffer, int totalParticles, float localAverage, int thresholdValue){
            MPI_Comm_rank(MPI_COMM_WORLD, &(this->nodeRank));
            this->parentRank = parentRank;
            this->chunkSize = chunkSize;
            inWindowBuffer = new int[MAX_STEAL];
            outWindowBuffer = new int[MAX_STEAL];
            this->recvBuffer = recvBuffer;
            status = STABLE;
            this->totalParticles = totalParticles;
            this->localAverage = localAverage;
            this->thresholdValue = thresholdValue;
            this->localThreshold = (localAverage*thresholdValue)/100;
            sentFlag = new bool[4];
            sentFlag[0] = 0;
            sentFlag[1] = 0;
            sentFlag[2] = 1;
            sentFlag[3] = 0;
            done = false;
            
            MPI_Win_create(inWindowBuffer, sizeof(int) * MAX_STEAL, sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &(this->inWindow));
            MPI_Win_create(outWindowBuffer, sizeof(int) * MAX_STEAL, sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &(this->outWindow));
        }

        /**
         * @brief Destroy the Node object
         * 
         * Core objects present in all node specialization are destroyed here:
         *  - recvBuffer
         *  - outWindowBuffer
         *  - inWindowBuffer
         *  - sentFlag
         */
        virtual ~Node(){
            delete []recvBuffer;
            delete []outWindowBuffer;
            delete []inWindowBuffer;
            delete []sentFlag;

            cout << "DELETION SUCCESSFUL OF NODE : " << nodeRank << endl; 
        }
};

#endif