#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include "common.h"

void* triage_worker(void* arg) {
    int id = *(int*)arg; free(arg);
    char msg[256];
    
    sprintf(msg, "[Triagem #%d] Iniciou turno.\n", id);
    write_log(msg);
    
    while (1) {
        // Verificar se devemos sair (Shutdown E Fila Vazia)
        sem_wait(&sem_mutex_triage); // Apenas leitura, mas protegido
        int pendentes = shm_data->pacientes_em_triagem;
        sem_post(&sem_mutex_triage);

        if (!shm_data->running && pendentes == 0) {
            break;
        }

        // Se sistema ativo, verificar tempo
        if (shm_data->running && difftime(time(NULL), shm_data->start_time) >= conf_shift) {
            sprintf(msg, "[Triagem #%d] Turno acabou!\n", id);
            write_log(msg);
            break;
        }

        if (sem_trywait(&sem_triage_fill) == 0) {
            sem_wait(&sem_mutex_triage); 
            Patient p = shm_data->triage_queue[shm_data->t_out];
            shm_data->t_out = (shm_data->t_out + 1) % conf_queue_max;
            shm_data->pacientes_em_triagem--;
            sem_post(&sem_mutex_triage);
            sem_post(&sem_triage_empty);

            // Calcular espera
            time_t now = time(NULL);
            sem_wait(&sem_mutex_stats);
            shm_data->total_triados++;
            shm_data->sum_wait_triage += (now - p.arrival_time);
            sem_post(&sem_mutex_stats);

            sprintf(msg, "[Triagem #%d] A avaliar %s (%ds)...\n", id, p.nome, p.tempo_triagem);
            write_log(msg);
            
            usleep(p.tempo_triagem * 1000000); 

            sem_wait(&sem_mutex_stats);
            shm_data->pacientes_em_espera_med++;
            int fila_atual = shm_data->pacientes_em_espera_med;
            sem_post(&sem_mutex_stats);

            if (fila_atual > conf_msq_wait_max) kill(getpid(), SIGUSR2);
            
            p.end_triage_time = time(NULL); 
            mq_send(mq_doctor, (char*)&p, sizeof(Patient), p.prioridade);
            
            sprintf(msg, "[Triagem #%d] %s enviado p/ MSQ (P%d).\n", id, p.nome, p.prioridade);
            write_log(msg);
        } else {
            usleep(100000);
        }
    }
    
    sprintf(msg, "[Triagem #%d] A sair (Shutdown/Fim).\n", id);
    write_log(msg);
    return NULL;
}