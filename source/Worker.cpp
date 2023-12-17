#include "Worker.hpp"
#include "utils.hpp"
#include "mpi.h"
#include <vector>
#include <random>
#include <cstdlib>
#include <thread>
#include <atomic>


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
int local_reduction(vector<int> *buffer, float start_average, int start_treshold){
	bool sentFlag = false;
	float local_average = start_average;
	int threshold = start_treshold;
	atomic<bool> done = false;

	calculate_time();
	cout << "WORKER THRESHOLD BEGINS AT : " << start_treshold << endl;
	random_device random_dev;
	thread status_thread;
	thread reciever_thread;
	default_random_engine random_eng(random_dev());
	uniform_int_distribution<int> uniform_dist(0, 0);

	//Qui si lanciano i thread
	status_thread = thread(sendStatusFunction, buffer, &local_average, &threshold, &done);
	reciever_thread = thread(recieveMessageFromMatchmaker, &local_average, &threshold, &done);

	int accumulated_result = 0;
	
	calculate_time();
	cout << "BUFFER SIZE : " << buffer->size() << endl;

	int buffer_size;

	while(!done){
		if(buffer->size() > 0){
			accumulated_result += buffer->back();
			buffer->pop_back();

			//calculate_time();
			//cout << "BUFFER SIZE: " << buffer->size() << endl;

			int val = uniform_dist(random_eng);
			probabilityIncreaseVectorSize(buffer, val);
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
void recieveMessageFromMatchmaker(float * local_average, int * threshold, atomic<bool> *done){
	MPI_Request request;
	MPI_Status stat1;
	MPI_Status stat2;
	int flag = false;
	float bufferAvg;

	calculate_time();
	cout << "STARTING RECIEVER THREAD IN WORKER" << endl;

	MPI_Recv(&bufferAvg, 1, MPI_FLOAT, 0, AVERAGE, MPI_COMM_WORLD, &stat1);

	while(bufferAvg > 0){

		*local_average = bufferAvg;
		
		calculate_time();
		cout << "WORKER RECIEVED AN UPDATE BEARING TAG : " << stat1.MPI_TAG << " WITH VALUE : " << bufferAvg << endl;
		MPI_Recv(&bufferAvg, 1, MPI_FLOAT, 0, AVERAGE, MPI_COMM_WORLD, &stat1);
		cout << "WAITING TO RECIEVE METRIC" << endl; 

		if(bufferAvg == 0)
			break;
	}

	*done = true;
	calculate_time();
	cout << "SETTING ENDING FLAG TO : " << *done << endl;

	calculate_time();
	cout << "CLOSING RECIEVE THREAD" << endl;
}