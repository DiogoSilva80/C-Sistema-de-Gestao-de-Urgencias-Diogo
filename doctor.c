#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "common.h"

void doctor_process(int id, int is_temp) {
    char msg[256];
    time_t doctor_start_time = time(NULL); 
    
    // REQUISITO: PID no ecrã e log ao iniciar
    sprintf(msg, "[Médico #%d (PID %d)] Iniciou turno.\n", id, getpid());
    write_log(msg);
    
    // LOOP RESILIENTE: Continua se running=1 OU se ainda houver pacientes
    while (1) {
        
        // Se running=0 (Shutdown) E fila vazia -> Sai
        sem_wait(&sem_mutex_stats);
        int espera = shm_data->pacientes_em_espera_med;
        int system_running = shm_data->running;
        sem_post(&sem_mutex_stats);

        if (!system_running && espera == 0) {
            break;
        }

        // Verificação Turno (apenas se o sistema estiver a correr normalmente)
        if (system_running && difftime(time(NULL), doctor_start_time) >= conf_shift) {
            // REQUISITO: PID no log ao terminar
            sprintf(msg, "[Médico #%d (PID %d)] Fim de turno (Tempo).\n", id, getpid());
            write_log(msg);
            exit(0); 
        }

        // Verificação Temporário
        if (is_temp) {
            if (espera < (0.8 * conf_msq_wait_max)) break;
        }

        Patient p;
        unsigned int prio;
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 1; 

        ssize_t bytes = mq_timedreceive(mq_doctor, (char*)&p, sizeof(Patient), &prio, &timeout);

        if (bytes >= 0) {
            time_t start_consult = time(NULL);
            long wait_for_doc = start_consult - p.end_triage_time;
            
            sprintf(msg, "[Médico #%d] A TRATAR %s (Espera: %lds).\n", id, p.nome, wait_for_doc);
            write_log(msg);
            
            usleep(p.tempo_medico * 1000000); 

            sem_wait(&sem_mutex_stats);
            shm_data->total_atendidos++;
            shm_data->total_tempo_medico += p.tempo_medico;
            shm_data->pacientes_em_espera_med--;
            
            shm_data->sum_wait_doctor += wait_for_doc;
            shm_data->sum_total_time += (time(NULL) - p.arrival_time);
            sem_post(&sem_mutex_stats);

            sprintf(msg, "[Médico #%d] Alta de %s.\n", id, p.nome);
            write_log(msg);
        }
    }
    
    // Log final de saída por Shutdown
    sprintf(msg, "[Médico #%d (PID %d)] A sair (Shutdown).\n", id, getpid());
    write_log(msg);
}