/*****************************************************
 * Copyright Grégory Mounié 2008-2015                *
 *           Simon Nieuviarts 2002-2009              *
 * This code is distributed under the GLPv3 licence. *
 * Ce code est distribué sous la licence GPLv3+.     *
 *****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h> // Pour execvp et fork
#include <sys/wait.h> // Pour waitpid et wait
#include <fcntl.h> //Pour la manipulation de mes fichiers au niveau de la question 6

#include "variante.h"
#include "readcmd.h"

#ifndef VARIANTE
#error "Variante non défini !!"
#endif

/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
 * following lines.  You may also have to comment related pkg-config
 * lines in CMakeLists.txt.
 */

// ========================================================================================
// QUESTION 1 : Lancement d'une commande
// QUESTION 2 : Lancement tache de fond ( Attente de terminaison)
// QUESTION 3 : Lancement tache de fond 
// QUESTION 4 : Lister les processus en tâches de fond
// QUESTION 5 : Pipe

#define MAX_JOBS 100

typedef struct {
    pid_t pid;
    char *command;
} Job;


Job jobs[MAX_JOBS];
int job_count = 0;


// Ajoute un nouveau processus en tâche de fond à la liste des jobs.
void ajouter_job(pid_t pid, char *command) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].pid = pid;
        jobs[job_count].command = strdup(command);  // Allouer une nouvelle chaîne pour la commande
        job_count++;
    } else {
        printf("Erreur : trop de tâches en arrière-plan.\n");
    }
}


// Parcourt tous les processus en tâche de fond et retire de la liste des jobs et sa mémoire est libérée si un processus est terminé (result != 0),
void verifier_jobs() {
    for (int i = 0; i < job_count; i++) {
        int status;
        pid_t result = waitpid(jobs[i].pid, &status, WNOHANG);
        if (result == -1) {
            perror("waitpid error");
        } else if (result > 0) {
            printf("Processus %d terminé.\n", jobs[i].pid);
            free(jobs[i].command);
            for (int j = i; j < job_count - 1; j++) {
                jobs[j] = jobs[j + 1];
            }
            job_count--;
            i--;  // Vérifier à nouveau l'indice car la liste est décalée
        }
    }
}


// Affiche la liste des processus en tâche de fond.
void lister_jobs() {
    printf("Liste des processus en tâche de fond :\n");
    for (int i = 0; i < job_count; i++) {
        printf("PID: %d, Commande: %s\n", jobs[i].pid, jobs[i].command);
    }
}

// =======================================================================================================

// GESTION DES PIPES

void executer_command_pipe(struct cmdline *l) {
    if (l == NULL || l->seq == NULL || l->seq[0] == NULL || l->seq[1] == NULL) {
        return;  // Pas de commande à exécuter
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    // Fork pour la première commande (écrivain)
    pid_t pid1 = fork();
    if (pid1 == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid1 == 0) {
        // Processus enfant pour la première commande
        close(pipefd[0]); // Fermer le côté lecture du pipe
        dup2(pipefd[1], STDOUT_FILENO); // Rediriger la sortie standard vers le pipe
        close(pipefd[1]); // Fermer le descripteur du pipe après redirection

        execvp(l->seq[0][0], l->seq[0]);
        // Si execvp échoue
        perror("execvp");
        exit(EXIT_FAILURE);
    }

    // Fork pour la seconde commande (lecteur)
    pid_t pid2 = fork();
    if (pid2 == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid2 == 0) {
        // Processus enfant pour la seconde commande
        close(pipefd[1]); // Fermer le côté écriture du pipe
        dup2(pipefd[0], STDIN_FILENO); // Rediriger l'entrée standard depuis le pipe
        close(pipefd[0]); // Fermer le descripteur du pipe après redirection

        execvp(l->seq[1][0], l->seq[1]);
        // Si execvp échoue
        perror("execvp");
        exit(EXIT_FAILURE);
    }

    // Processus parent : fermer les deux descripteurs du pipe
    close(pipefd[0]);
    close(pipefd[1]);

    // Attendre la fin des deux processus enfants
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}



//=======================================================================================

// Redirection des fichiers

void executer_command_redirection(struct cmdline *l) {
    if (l == NULL || l->seq == NULL || l->seq[0] == NULL) {
        return;  // Pas de commande à exécuter
    }

    char **cmd = l->seq[0];  // Première commande dans la séquence
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // Processus enfant

        // Redirection de l'entrée standard si nécessaire
        if (l->in) {
            int fd_in = open(l->in, O_RDONLY);
            if (fd_in < 0) {
                perror("open in");
                exit(EXIT_FAILURE);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in); // Fermer le descripteur après duplication
        }

        // Redirection de la sortie standard si nécessaire
        if (l->out) {
            int fd_out = open(l->out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out < 0) {
                perror("open out");
                exit(EXIT_FAILURE);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out); // Fermer le descripteur après duplication
        }

        // Exécuter la commande
        execvp(cmd[0], cmd);
        perror("execvp");
        exit(EXIT_FAILURE);
    } else {
        // Processus parent : attendre la fin si la commande est en avant-plan
        if (!l->bg) {
            waitpid(pid, NULL, 0);
        } else {
            ajouter_job(pid, cmd[0]);
            printf("[Processus en tâche de fond lancé: PID %d]\n", pid);
        }
    }
}



// Modifications dans executer_command pour appeler executer_command_redirection
void executer_command(struct cmdline *l) {
    if (l == NULL || l->seq == NULL || l->seq[0] == NULL) {
        return;
    }

    int i = 0;
    int pipefd[2] = {-1, -1}; // Initialisation du pipe à des valeurs non valides
    int input_fd = -1;        // Le descripteur d'entrée initial est nul (-1)
    pid_t pids[MAX_JOBS];
    int num_pids = 0;

    while (l->seq[i] != NULL) {
        char **cmd = l->seq[i];
        pid_t pid;

        if (l->seq[i + 1] != NULL) {
            // Créer un pipe si une autre commande suit
            if (pipe(pipefd) == -1) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }

        pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {
            // Processus enfant : gestion des redirections et des pipes
            if (input_fd != -1) {
                // Rediriger l'entrée depuis le pipe précédent
                if (dup2(input_fd, STDIN_FILENO) == -1) {
                    perror("dup2 (input)");
                    exit(EXIT_FAILURE);
                }
                close(input_fd);
            }

            if (l->seq[i + 1] != NULL) {
                // Si une commande suit, rediriger la sortie vers le pipe
                close(pipefd[0]); // Fermer le côté lecture du pipe
                if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                    perror("dup2 (output)");
                    exit(EXIT_FAILURE);
                }
                close(pipefd[1]);
            }

            // Gestion des redirections d'entrée et de sortie depuis/vers des fichiers
            if (l->in != NULL && i == 0) {
                // Redirection de l'entrée pour la première commande
                int fd_in = open(l->in, O_RDONLY);
                if (fd_in == -1) {
                    perror("open (input file)");
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd_in, STDIN_FILENO) == -1) {
                    perror("dup2 (input file)");
                    exit(EXIT_FAILURE);
                }
                close(fd_in);
            }

            if (l->out != NULL && l->seq[i + 1] == NULL) {
                // Redirection de la sortie pour la dernière commande
                int fd_out = open(l->out, O_WRONLY | O_CREAT, 0644);
                if (fd_out == -1) {
                    perror("open (output file)");
                    exit(EXIT_FAILURE);
                }
                
                // Troncature du fichier pour supprimer son contenu précédent
                if (ftruncate(fd_out, 0) == -1) {
                    perror("ftruncate");
                    exit(EXIT_FAILURE);
                }

                if (dup2(fd_out, STDOUT_FILENO) == -1) {
                    perror("dup2 (output file)");
                    exit(EXIT_FAILURE);
                }
                close(fd_out);
            }

            execvp(cmd[0], cmd);
            perror("execvp");
            exit(EXIT_FAILURE);
        } else {
            // Processus parent
            pids[num_pids++] = pid;

            // Fermer les descripteurs inutilisés
            if (input_fd != -1) {
                close(input_fd);
            }
            if (l->seq[i + 1] != NULL) {
                close(pipefd[1]); // Fermer le côté écriture du pipe dans le parent
                input_fd = pipefd[0]; // Garder le côté lecture du pipe pour la prochaine commande
            }
        }
        i++;
    }

    // Attendre la fin de tous les processus enfants, sauf si en arrière-plan
    if (!l->bg) {
        for (int j = 0; j < num_pids; j++) {
            waitpid(pids[j], NULL, 0);
        }
    } else {
        ajouter_job(pids[num_pids - 1], l->seq[0][0]);
        printf("[Processus en tâche de fond lancé: PID %d]\n", pids[num_pids - 1]);
    }
}



// ========================================================================================

#if USE_GUILE == 1
#include <libguile.h>

int question6_executer(char *line)
{
	/* Question 6: Insert your code to execute the command line
	 * identically to the standard execution scheme:
	 * parsecmd, then fork+execvp, for a single command.
	 * pipe and i/o redirection are not required.
	 */

	// Parse la ligne de commande
    struct cmdline *cmd = parsecmd(&line);
    if (cmd == NULL || cmd->seq[0] == NULL) {
        free(line);
        return -1; // Rien à exécuter
    }

    // Créer un processus fils
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        free(line);
        return -1;
    }

	/* Remove this line when using parsecmd as it will free it */
	free(line);
	
	return 0;
}

SCM executer_wrapper(SCM x)
{
        return scm_from_int(question6_executer(scm_to_locale_stringn(x, 0)));
}
#endif


void terminate(char *line) {
#if USE_GNU_READLINE == 1
	/* rl_clear_history() does not exist yet in centOS 6 */
	clear_history();
#endif
	if (line)
	  free(line);
	printf("exit\n");
	exit(0);
}


int main() {
        printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#if USE_GUILE == 1
        scm_init_guile();
        /* register "executer" function in scheme */
        scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

	while (1) {
		struct cmdline *l;
		char *line=0;
		int i, j;
		char *prompt = "ensishell>";

		//************** Vérifier les processus en tâche de fond **************
        verifier_jobs();

		/* Readline use some internal memory structure that
		   can not be cleaned at the end of the program. Thus
		   one memory leak per command seems unavoidable yet */
		line = readline(prompt);
		if (line == 0 || ! strncmp(line,"exit", 4)) {
			terminate(line);
		}

		//*********** Vérifier si la commande est 'jobs' ***************
        if (strcmp(line, "jobs") == 0) {
            lister_jobs();  // Appeler la fonction qui liste les jobs
            free(line);
            continue;  // Retourner au prompt
        }

#if USE_GNU_READLINE == 1
		add_history(line);
#endif


#if USE_GUILE == 1
		/* The line is a scheme command */
		if (line[0] == '(') {
			char catchligne[strlen(line) + 256];
			sprintf(catchligne, "(catch #t (lambda () %s) (lambda (key . parameters) (display \"mauvaise expression/bug en scheme\n\")))", line);
			scm_eval_string(scm_from_locale_string(catchligne));
			free(line);
                        continue;
                }
#endif

		/* parsecmd free line and set it up to 0 */
		l = parsecmd( & line);

		/* If input stream closed, normal termination */
		if (!l) {
		  
			terminate(0);
		}
		

		
		if (l->err) {
			/* Syntax error, read another command */
			printf("error: %s\n", l->err);
			continue;
		}

		if (l->in) printf("in: %s\n", l->in);
		if (l->out) printf("out: %s\n", l->out);
		if (l->bg) printf("background (&)\n");

		/* Display each command of the pipe */
		for (i=0; l->seq[i]!=0; i++) {
			char **cmd = l->seq[i];
			printf("seq[%d]: ", i);
                        for (j=0; cmd[j]!=0; j++) {
                                printf("'%s' ", cmd[j]);
                        }
			printf("\n");
		}

		//============================================================================================
		// Execution de la commande 
		executer_command(l);
	}

	return 0;

}
