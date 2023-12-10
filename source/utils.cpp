#include "utils.hpp"

/**
 * @brief Semplice funzione per la temporizzazione
 * 
 * Questa funzione fa uso della libreria chrono e delle utilities fornite per aggiungere dei timestamp ai log su file
 * il tempo viene contato in millisecondi trascorsi dall'inizio della computazione determinata nel main
 */
void calculate_time(){
    std::chrono::duration<double> elapsed = std::chrono::system_clock::now() - start;
	std::cout << elapsed.count() << "s ";
};