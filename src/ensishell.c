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

#include <glob.h> // Pour l'expansion des jokers

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




#define MAX_JOBS 100

typedef struct {
    pid_t pid;
    char *command;
} Job;


Job jobs[MAX_JOBS];
int job_count = 0;


// ================================================================================================
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


// ================================================================================================
// Fonction pour vérifier les jobs en tâche de fond
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

// ================================================================================================
// Fonction pour afficher les jobs en tâche de fond
void lister_jobs() {
    printf("Liste des processus en tâche de fond :\n");
    for (int i = 0; i < job_count; i++) {
        printf("PID: %d, Commande: %s\n", jobs[i].pid, jobs[i].command);
    }
}


// ================================================================================================
// Question 6  : Redirection

// Fonction pour gérer les redirections d'entrée et de sortie
void gerer_redirections(struct cmdline *l, int i, int input_fd, int pipefd[2]) {

    // Gestion des redirections et des pipes
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
}

// ================================================================================================
// Fonction pour exécuter une commande enfant
void executer_enfant(char **cmd) {
    execvp(cmd[0], cmd);
    perror("execvp");
    exit(EXIT_FAILURE);
}


// =================================================================================================
// Question 8  : Jokers étendus (Jocker en glob)


// Fonction pour gérer l'expansion des jokers et des accolades dans une commande
char **expand_command(char **cmd) {
    glob_t glob_result;
    int glob_flags = GLOB_NOCHECK | GLOB_TILDE; // Flags pour gérer les jokers et le tilde
    char **expanded_cmd = NULL;
    size_t expanded_count = 0;

    // Initialiser glob_result
    memset(&glob_result, 0, sizeof(glob_result));

    // Expansion des accolades avant d'appeler glob
    for (int i = 0; cmd[i] != NULL; i++) {
        char *brace_expanded = NULL;
        if (strchr(cmd[i], '{') != NULL && strchr(cmd[i], '}') != NULL) {
            // Gérer les accolades manuellement
            char *brace_pos = strchr(cmd[i], '{');
            char *brace_end = strchr(cmd[i], '}');
            if (brace_pos && brace_end && brace_end > brace_pos) {
                size_t prefix_len = brace_pos - cmd[i];
                size_t suffix_len = strlen(brace_end + 1);

                char *inside_braces = strndup(brace_pos + 1, brace_end - brace_pos - 1);
                char *token = strtok(inside_braces, ",");
                while (token != NULL) {
                    size_t new_len = prefix_len + strlen(token) + suffix_len + 1;
                    brace_expanded = malloc(new_len);
                    if (!brace_expanded) {
                        perror("malloc");
                        free(inside_braces);
                        globfree(&glob_result);
                        return expanded_cmd; // En cas d'échec, retourner la commande d'origine
                    }
                    snprintf(brace_expanded, new_len, "%.*s%s%s", (int)prefix_len, cmd[i], token, brace_end + 1);
                    
                    char **temp_cmd = realloc(expanded_cmd, sizeof(char *) * (expanded_count + 2));
                    if (!temp_cmd) {
                        perror("realloc");
                        free(brace_expanded);
                        free(inside_braces);
                        globfree(&glob_result);
                        for (size_t j = 0; j < expanded_count; j++) {
                            free(expanded_cmd[j]);
                        }
                        free(expanded_cmd);
                        return cmd; // En cas d'échec, retourner la commande d'origine
                    }
                    expanded_cmd = temp_cmd;
                    expanded_cmd[expanded_count++] = brace_expanded;
                    token = strtok(NULL, ",");
                }
                free(inside_braces);
                continue;
            }
        }

        // Ajouter la commande telle quelle si aucune accolade n'est présente
        char **temp_cmd = realloc(expanded_cmd, sizeof(char *) * (expanded_count + 2));
        if (!temp_cmd) {
            perror("realloc");
            globfree(&glob_result);
            for (size_t j = 0; j < expanded_count; j++) {
                free(expanded_cmd[j]);
            }
            free(expanded_cmd);
            return cmd; // En cas d'échec, retourner la commande d'origine
        }
        expanded_cmd = temp_cmd;
        expanded_cmd[expanded_count++] = strdup(cmd[i]);
    }
    expanded_cmd[expanded_count] = NULL;

    // Expansion des jokers pour chaque argument
    for (int i = 0; expanded_cmd[i] != NULL; i++) {
        int glob_status;
        if (i == 0) {
            // Pour le premier argument, on initie glob
            glob_status = glob(expanded_cmd[i], glob_flags, NULL, &glob_result);
        } else {
            // Pour les arguments suivants, on ajoute au résultat existant
            glob_status = glob(expanded_cmd[i], glob_flags | GLOB_APPEND, NULL, &glob_result);
        }

        if (glob_status != 0) {
            perror("glob");
            globfree(&glob_result);
            for (size_t j = 0; j < expanded_count; j++) {
                free(expanded_cmd[j]);
            }
            free(expanded_cmd);
            return cmd; // En cas d'échec, retourner la commande d'origine
        }
    }

    // Libérer les anciennes commandes après l'expansion
    for (int i = 0; expanded_cmd[i] != NULL; i++) {
        free(expanded_cmd[i]);
    }
    free(expanded_cmd);

    return glob_result.gl_pathv; // Retourner les chemins expansés
}



// ================================================================================================
// Question 10  : Signaux

// Gestionnaire de signal pour SIGCHLD
void gestionnaire_sigchld(int sig) {
    (void)sig; // Ignorer l'argument du signal
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].pid == pid) {
                printf("[Processus %d terminé]\n", pid);
                free(jobs[i].command);
                for (int j = i; j < job_count - 1; j++) {
                    jobs[j] = jobs[j + 1];
                }
                job_count--;
                break;
            }
        }
    }
}


// ================================================================================================


// Fonction pour exécuter une commande
void executer_command(struct cmdline *l) {
    if (l == NULL || l->seq == NULL || l->seq[0] == NULL) {
        return;
    }

    // QUESTION 5 : Pipe

    int i = 0;
    int pipefd[2] = {-1, -1}; // Initialisation du pipe à des valeurs non valides
    int input_fd = -1;        // Le descripteur d'entrée initial est nul (-1)
    pid_t pids[MAX_JOBS];
    int num_pids = 0;

    while (l->seq[i] != NULL) {
        // Expansion des jokers pour la commande actuelle
        char **cmd = expand_command(l->seq[i]);
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
            gerer_redirections(l, i, input_fd, pipefd);
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

// Partie 5 : Appel de l'interpreteur Scheme

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

    if (pid == 0) {
        // Processus enfant : exécuter la commande
        execvp(cmd->seq[0][0], cmd->seq[0]);
        perror("execvp"); // Si execvp échoue
        exit(EXIT_FAILURE);
    } else {
        // Processus parent : attendre la fin de la commande
        waitpid(pid, NULL, 0);
    }

	/* Remove this line when using parsecmd as it will free it */
	free(line);
	
	return 0;
}


// ================================================================================================
SCM executer_wrapper(SCM x)
{
        return scm_from_int(question6_executer(scm_to_locale_stringn(x, 0)));
}
#endif


// ================================================================================================
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


// ================================================================================================

int main() {
        printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#if USE_GUILE == 1
        scm_init_guile();
        /* register "executer" function in scheme */
        scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

    // ------Définir le gestionnaire pour SIGCHLD pour la terminaison asynchrone
    struct sigaction sa;
    sa.sa_handler = gestionnaire_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

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
