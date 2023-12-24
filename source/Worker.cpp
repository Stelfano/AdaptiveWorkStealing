#include "Worker.hpp"
#include "utils.hpp"
#include "mpi.h"
#include <vector>
#include <random>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <cstring>


using namespace std;

/**
 * @brief Aggiunge elementi al vettore 
 *
 * Funzione di aggiunta degli elementi al vettore della riduzione, se negativo verrano eliminati degli elementi
 * 
 * @param buffer Vettore di riduzione
 * @param generatedNumber Numero di elementi da aggiungere
 */
void probabilityIncreaseVectorSize(vector<int> *buffer, int generatedNumber){

	if(buffer->size() < abs(generatedNumber))
		return;
	
	if(generatedNumber > 0){
		for(int i = 0;i < generatedNumber;i++){
			buffer->push_back(1);
		}
	}

	if(generatedNumber < 0){
		for(int i = 0;i < abs(generatedNumber);i++){
			buffer->pop_back();
		}
	}
}

//Non c'è veramente bisogno di usare una funzione
void declareStatus(int *buffer_size, int status){
	MPI_Send(buffer_size, 1, MPI_INT, 0, status, MPI_COMM_WORLD);
}


/**
 * @brief Funzione di riduzione locale per il worker
 *
 * Questa funzione determina il comportamento dei worker, all'interno viene eseguito il main loop della riduzione e vengono lanciati i
 * thread che regolano la comunicazione dei worker con il matchmaker e lo scambio della metrica
 * 
 * @param buffer Buffer di contenimento per il valore finale
 * @param start_average Valore iniziale distribuito a tutti i core
 * @param start_treshold Valore di threshold iniziale
 * @return int Valore finale computato
 */
int local_reduction(vector<int> *buffer, float start_average, int start_treshold, int* window_buffer, MPI_Win *win, int rank){
	bool sentFlag = false;
	float local_average = start_average;
	int threshold = start_treshold;
	atomic<bool> done = false;

	calculate_time();
	cout << "I AM RANK : " << rank << endl;
	calculate_time();
	cout << "WORKER THRESHOLD BEGINS AT : " << start_treshold << endl;
	random_device random_dev;
	thread status_thread;
	thread reciever_thread;
	default_random_engine random_eng(random_dev());
	uniform_int_distribution<int> uniform_dist(0, 2);

	//Qui si lanciano i thread
	status_thread = thread(sendStatusFunction, buffer, &local_average, &threshold, &done);
	reciever_thread = thread(recieveMessageFromMatchmaker, buffer, &local_average, &threshold, &done, window_buffer);

	int accumulated_result = 0;
	
	calculate_time();
	cout << "BUFFER SIZE : " << buffer->size() << endl;

	int buffer_size;

	while(!done){
		if(buffer->size() > 0){
			accumulated_result += buffer->back();
			buffer->pop_back();

			int val = uniform_dist(random_eng);
			if(rank != 2){
				probabilityIncreaseVectorSize(buffer, val);
			}

			if(buffer->size() < MAX_STEAL){
				copy(buffer->begin(), buffer->begin()+buffer->size(), window_buffer);	
				memset(window_buffer + buffer->size(), 0, MAX_STEAL - buffer->size() + sizeof(int));
			}else{
				copy(buffer->begin(), buffer->begin()+buffer->size(), window_buffer);
			}
		}
	}

	calculate_time();
	cout << "LOCAL REDUCTION HAS ENDED!" << endl;
	status_thread.join();
	reciever_thread.join();
		
	return accumulated_result;
}

/**
 * @brief Funzione che regola la comunicazione Worker -> Matchmaker
 *
 * Questa funzione lanciata da un thread all'interno del mainloop del worker serve a notificare il matchmaker dei cambi di stato di un worker
 * e inviare valori sulla metrica locale del worker
 * 
 * @param buffer Vettore contenete i valori da ridurre, usiamo la funzione size per indicare il numero di elementi rimanenti
 * @param local_average Media locale da comunicare al matchmaker
 * @param threshold Threshold usato per indicare lo stato al matchmaker
 */
void sendStatusFunction(vector<int> * buffer, float * local_average, int * threshold, atomic<bool> *done){
	bool sentFlag[4] = {0, 0, 0, 0};

	while(!(*done)){	
		int buffer_size = buffer->size();

		if(buffer_size > (*local_average + *threshold) && sentFlag[3] == false){
			declareStatus(&buffer_size, OVERWORK);
			sentFlag[3] = true;

			sentFlag[2] = false;
			sentFlag[1] = false;
			sentFlag[0] = false;
		}

		if(buffer_size <= (*local_average + *threshold) && buffer_size >= (*local_average - *threshold) && sentFlag[2] == false){
			declareStatus(&buffer_size, STABLE);
			sentFlag[2] = true;

			sentFlag[3] = false;
			sentFlag[1] = false;
			sentFlag[0] = false;
		}

		if(buffer_size < (*local_average - *threshold) && sentFlag[1] == false && buffer_size != 0){
			declareStatus(&buffer_size, UNDERWORK);
			sentFlag[1] = true;

			sentFlag[3] = false;
			sentFlag[2] = false;
			sentFlag[0] = false;
		}

		if(buffer_size == 0 && sentFlag[0] == false){
			calculate_time();
			cout << "I AM IDLE!" << endl;
			buffer_size = 0;
			declareStatus(&buffer_size, IDLE);
			sentFlag[0] = true;

			sentFlag[3] = false;
			sentFlag[2] = false;
			sentFlag[1] = false;

		}
	}

	calculate_time();
	cout << "SENDER THREAD TERMINATING" << endl;
}

//Funzione per la ricezione delle metriche da parte del matchmaker
void recieveMessageFromMatchmaker(vector<int> *buffer, float * local_average, int * threshold, atomic<bool> *done, int * window_buffer){
	MPI_Status stat1, stat2, stat3, stat4;
	MPI_Request req1, req2, req3, req4;
	int flag1 = false;
	int flag2 = false;
	int flag3 = false;
	int flag4 = false;
	float bufferAvg = *local_average;
	int bufferThreshold = *threshold;
	int stealingBuffer = 0;

	calculate_time();
	cout << "STARTING RECIEVER THREAD IN WORKER" << endl;

	MPI_Irecv(&bufferAvg, 1, MPI_FLOAT, 0, AVERAGE, MPI_COMM_WORLD, &req1);
	MPI_Irecv(&bufferThreshold, 1, MPI_INT, 0, THRESHOLD, MPI_COMM_WORLD, &req2);
	MPI_Irecv(&stealingBuffer, 1, MPI_INT, 0, VICTIM, MPI_COMM_WORLD, &req3);
	MPI_Irecv(&stealingBuffer, 1, MPI_INT, 0, TARGET, MPI_COMM_WORLD, &req4);

	while(bufferAvg > 0){
		MPI_Test(&req1, &flag1, &stat1);
		MPI_Test(&req2, &flag2, &stat2);
		MPI_Test(&req3, &flag3, &stat3);
		MPI_Test(&req4, &flag4, &stat4);

		if(flag1 == true){
			MPI_Irecv(&bufferAvg, 1, MPI_FLOAT, 0, AVERAGE, MPI_COMM_WORLD, &req1);
			calculate_time();
			//cout << "WORKER RECIEVED AN UPDATE BEARING TAG : " << stat1.MPI_TAG << " WITH VALUE : " << bufferAvg << endl;
			*local_average = bufferAvg;
			flag1 = false;
		}

		if(flag2 == true){
			MPI_Irecv(&bufferThreshold, 1, MPI_INT, 0, THRESHOLD, MPI_COMM_WORLD, &req2);
			calculate_time();
			//cout << "WORKER RECIEVED AN UPDATE BEARING TAG : " << stat2.MPI_TAG << " WITH VALUE : " << bufferThreshold << endl;
			*threshold = bufferThreshold;
			flag2 = false;
		}

		if(flag3 == true){
			MPI_Irecv(&stealingBuffer, 1, MPI_INT, 0, VICTIM, MPI_COMM_WORLD, &req3);
			calculate_time();
			cout << "WORKER RECIEVED AN UPDATE BEARING TAG : " << stat3.MPI_TAG << " WITH VALUE : " << stealingBuffer << " IT'S STEALING TIME" << endl;
			calculate_time();
			cout << "NUMBER OF ELEMENTS AT STEALING TIME : " << buffer->size() << endl;
			//Addition of a mutex
			buffer->erase(buffer->begin(), buffer->begin() + stealingBuffer);
			calculate_time();
			cout << "VICTIM NEW BUFFER SIZE : " << buffer->size() << endl;
			
			stealingBuffer = buffer->size() - stealingBuffer;
			declareStatus(&stealingBuffer, UNLOCKED);

			flag3 = false;	
		}

		if(flag4 == true){
			MPI_Irecv(&stealingBuffer, 1, MPI_INT, 0, TARGET, MPI_COMM_WORLD, &req4);
			calculate_time();
			cout << "WORKER RECIEVED AN UPDATE BEARING TAG : " << stat4.MPI_TAG << " WITH VALUE : " << stealingBuffer << " IT'S STEALING TIME" << endl;
			buffer->insert(buffer->end(), window_buffer, window_buffer + stealingBuffer);
			calculate_time();
			cout << "TARGET NEW BUFFER SIZE : " << buffer->size()  << endl;

			stealingBuffer = buffer->size() + stealingBuffer;
			declareStatus(&stealingBuffer, UNLOCKED);

			flag4 = false;	
		}

	}

	*done = true;
	MPI_Cancel(&req3);
	MPI_Cancel(&req4);
	calculate_time();
	cout << "CLOSING RECIEVE THREAD" << endl;
}
