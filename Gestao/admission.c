// Diogo António Mesquita Ferrão da Silva 2024199827
// João Pedro Augusto Palmeiro	2023223918

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h> 
#include "common.h"

void process_patient(Patient p) {
    char msg[256];
    
    // CORREÇÃO: Usar a variável dinâmica conf_shift
    if (difftime(time(NULL), shm_data->start_time) >= conf_shift) {
        write_log("[Admissão] Turno Encerrado! Paciente rejeitado.\n");
        return;
    }

    if (sem_trywait(&sem_triage_empty) == 0) {
        
        sem_wait(&sem_mutex_triage);
        
        shm_data->triage_queue[shm_data->t_in] = p;
        // CORREÇÃO: conf_queue_max agora é reconhecido
        shm_data->t_in = (shm_data->t_in + 1) % conf_queue_max; 
        shm_data->pacientes_em_triagem++;
        
        sem_post(&sem_mutex_triage);
        sem_post(&sem_triage_fill);
        
        sprintf(msg, "[Entrada] %s admitido (ID %d).\n", p.nome, p.id);
        write_log(msg);
        
    } else {
        sprintf(msg, "[ERRO] Fila triage_queue cheia! %s eliminado.\n", p.nome);
        write_log(msg);
    }
}

void start_admission() {
    if (access(PIPE_NAME, F_OK) == -1) mkfifo(PIPE_NAME, 0666);
    int pipe_fd = open(PIPE_NAME, O_RDONLY);

    write_log("[Admissão] À espera no input_pipe...\n");

    char buffer[256];
    char msg[256];
    static int global_counter = 0;

    while (shm_data->running) {
        ssize_t n = read(pipe_fd, buffer, sizeof(buffer)-1);
        if (n > 0) {
            buffer[n] = '\0';
            
            // Comandos
            char command[20];
            int qtd;
            if (sscanf(buffer, "%s %d", command, &qtd) == 2 && strcmp(command, "NEW_DOCTOR") == 0) {
                sprintf(msg, "[GESTÃO] Comando recebido: Criar %d Médicos.\n", qtd);
                write_log(msg);
                for(int i=0; i<qtd; i++) spawn_doctor(shm_data->active_doctors + 1, 0);
                continue; 
            }

            // Pacientes
            // Verifica digito (Grupo) ou letra (Nome)
            if (isdigit(buffer[0])) {
                int count, t_t, t_m, prio;
                if (sscanf(buffer, "%d %d %d %d", &count, &t_t, &t_m, &prio) == 4) {
                    sprintf(msg, "[Admissão] Grupo de %d pacientes.\n", count);
                    write_log(msg);
                    for (int i = 0; i < count; i++) {
                        Patient p;
                        global_counter++;
                        p.id = global_counter;
                        p.tempo_triagem = t_t;
                        p.tempo_medico = t_m;
                        p.prioridade = prio;
                        p.arrival_time = time(NULL);
                        sprintf(p.nome, "Auto-%d", global_counter);
                        process_patient(p);
                        usleep(10000); 
                    }
                }
            } else {
                Patient p;
                if (sscanf(buffer, "%s %d %d %d", p.nome, &p.tempo_triagem, &p.tempo_medico, &p.prioridade) == 4) {
                    global_counter++;
                    p.id = global_counter;
                    p.arrival_time = time(NULL);
                    process_patient(p);
                }
            }
        }
    }
    close(pipe_fd);
}