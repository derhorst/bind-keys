#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <string.h>

#include <stdlib.h>
#include <libconfig.h>

#include <time.h>
#include <sys/wait.h>

#include <pthread.h>

typedef struct node {
  const char *command;
  time_t execute_at;
  struct node *next;
} node;

int show_keys = 0;
char *mode;
node *delayed;
config_t cfg;
config_setting_t *key_binds;
const char *keyboard;
int wait_to_execute = 0;

int read_config();
int read_input(const char *keyboard, config_setting_t *key_binds);
int add_key_pressed(struct input_event *keys, struct input_event *ev);
int remove_key_pressed(struct input_event *keys, struct input_event *ev);
int get_action(struct input_event *keys, config_setting_t *key_binds);
node *add_item(node *listpointer, const char *command, time_t execute_at);
node *remove_item(node *listpointer);
void* wait_to_exec(void *ptr);
void check_queue(node *listpointer);
void execute_command(const char *command);
int help();


int main(int argc, char **argv) {
  if(argc > 1) {
    argv[1][sizeof(argv[1])-1] = '\0';
    printf("%i\n", strcmp(argv[1], "-h"));
    if(strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
      help();
    } else if(strcmp(argv[1], "-s") == 0 || strcmp(argv[1], "--show-keys") == 0) {
      show_keys = 1;
    } else {
      printf("Unknown option: %s\n", argv[1]);
      help();
    }
  }

  /* create a second thread to check whether any delayed commands need to be exectued */
  pthread_t wait_to_exec_thread;
  pthread_create(&wait_to_exec_thread, NULL, &wait_to_exec, NULL);

  read_config();
  read_input(keyboard, key_binds);
  config_destroy(&cfg);
}

int read_input(const char *keyboard, config_setting_t *key_binds) {
  int fd;
  fd = open(keyboard, O_RDONLY);
  struct input_event ev;
  struct input_event keys[]= {
    { .type = 0 },
    { .type = 0 },
    { .type = 0 },
    { .type = 0 }
  };

  while (1) {
    read(fd, &ev, sizeof(struct input_event));
    if(ev.type != 0 && ev.type < 1000) {
      if(ev.value == 1) {
        add_key_pressed(keys, &ev);
      }

      if(ev.value == 0) {
        if(show_keys &&  ev.code != 0) {
          printf(" - %i \n", ev.code);
        }
        get_action(keys, key_binds);

        remove_key_pressed(keys, &ev);
      }
    }
  }
}

int get_action(struct input_event *keys, config_setting_t *key_binds) {
  /* Get all key-binds */
  if(key_binds != NULL) {
    unsigned int count = config_setting_length(key_binds);
    unsigned int i,j;
    // iterate over all key_binds
    for(i = 0; i < count; ++i) {
      config_setting_t *key_bind = config_setting_get_elem(key_binds, i);
      config_setting_t *config_keys = config_setting_get_member(key_bind, "keys");

      if(config_keys != NULL) {
        unsigned int key_count = config_setting_length(config_keys);
        // iterate over pressed keys
        unsigned int found = 0;
        for (int i = 0; i < 4; i++) {
          if(keys[i].type != 0) {
            for(j = 0; j < key_count; ++j) {
              int key_code = config_setting_get_int_elem(config_keys, j);
              if(keys[i].code == key_code) {
                found++;
                break;
              }
            }
          }
        }
        // if not all keys were pressed continue to next keybind
        if(found != key_count) {
          continue;
        } else {
          const char *command, *command_mode, *change_mode;
          int delay = 0;
          // execute command
          if(config_setting_lookup_string(key_bind, "command", &command)) {
            // if a mode is supplied check if it's satisfied
            if(config_setting_lookup_string(key_bind, "mode", &command_mode) && mode) {
              if(strcmp(command_mode, mode) == 0) {
                printf("execute %s - %s\n\n", command, mode);
                if(config_setting_lookup_int(key_bind, "delay", &delay)) {
                  delayed = add_item(delayed, command, time(NULL)+delay);
                  printf("delayed execution in %d seconds\n", delay);
                } else {
                  // fork() to create child process
                  execute_command(command);
                }
              }
            } else {
              printf("execute %s\n\n", command);
              if(config_setting_lookup_int(key_bind, "delay", &delay)) {
                delayed = add_item(delayed, command, time(NULL)+delay);
                printf("delayed execution in %d seconds\n", delay);
              } else {
                execute_command(command);
              }
            }
          }
          // change mode
          if(config_setting_lookup_string(key_bind, "change_mode", &change_mode)) {
            if(mode) {
              free(mode);
              mode = strdup(change_mode);
              printf("change_mode %s\n\n", change_mode);
            }
          }
        }
      } else {
        printf("Could not get keys\n");
      }
    }
  } else {
    printf("no key binds\n");
  }
  return 0;
}

int add_key_pressed(struct input_event *keys, struct input_event *ev) {
  for (int i = 0; i < 4; i++) {
    // remove keys that have been pressed for longer than 20 seconds
    if(ev->time.tv_sec - keys[i].time.tv_sec > 20) {
      keys[i].type = 0;
    }
    if(keys[i].type == 0) {
      keys[i] = *ev;
      return 0;
    }
  }
  return 0;
}

int remove_key_pressed(struct input_event *keys, struct input_event *ev) {
  for (int i = 0; i < 4; i++) {
    if(keys[i].code == ev->code) {
      keys[i].type = 0;
    }
  }
  return 0;
}

int read_config() {
  const char *default_mode;
  config_init(&cfg);
  /* Read the file. If there is an error, report it and exit. */
  if(! config_read_file(&cfg, "bind-keys.cfg")) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
       fprintf(stderr, "Could not read config file - Current working dir: %s\n", cwd);
    else
       perror("getcwd() error");

    fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
    config_error_line(&cfg), config_error_text(&cfg));
    config_destroy(&cfg);
    return(EXIT_FAILURE);
  }

  /* Get the keyboard name. */
  if(config_lookup_string(&cfg, "keyboard", &keyboard)) {
    printf("Keyboard: %s\n\n", keyboard);
  } else {
    fprintf(stderr, "No 'keyboard' setting in configuration file.\n");
    exit(1);
  }

  /* Get the default mode name. */
  if(config_lookup_string(&cfg, "default_mode", &default_mode)) {
    printf("mode: %s\n\n", default_mode);
    mode = strdup(default_mode);
  } else {
    printf("no default mode");
  }

  key_binds = config_lookup(&cfg, "key_binds");

  return 0;
}

void execute_command(const char *command) {
  /* double fork to avoid zombie processes */
  pid_t pid1;
  pid_t pid2;
  int status;

  if ((pid1 = fork())) {
    /* parent process */
    waitpid(pid1, &status, WSTOPPED);
  } else if (!pid1) {
    /* child process */
    if ((pid2 = fork())) {
      exit(0);
    } else if (!pid2) {
      /* child process B */
      system(command);
      exit(0);
    }
  }
}

void* wait_to_exec(void *ptr) {
  // check whether the delayed queue has any entries that need to be executed
  while(1) {
    if(delayed != NULL) {
      check_queue(delayed);
    }
    sleep(1);
  }
}

node *add_item(node *listpointer, const char *command, time_t execute_at) {
  node *lp = listpointer;
  if (listpointer != NULL) {
    while (listpointer->next != NULL) {
      // command already in queue, replace the execute at time
      if(strcmp(listpointer->command,command) == 0) {
        printf("execute at time updated\n");
        printf("old time %ld\n", listpointer->execute_at);
        listpointer->execute_at = execute_at;
        printf("new time %ld\n", listpointer->execute_at);
        return lp;
      }
      listpointer = listpointer->next;
    }
    listpointer->next = (struct node  *) malloc (sizeof (node));
    listpointer = listpointer->next;
    listpointer->next = NULL;
    listpointer->command = command;
    listpointer->execute_at = execute_at;
    return lp;
  } else {
    listpointer = (struct node  *) malloc (sizeof (node));
    listpointer->next = NULL;
    listpointer->command = command;
    listpointer->execute_at = execute_at;
    return listpointer;
  }
}

node *remove_item(node *listpointer) {
  node *tempp;
  tempp = listpointer->next;
  free (listpointer);
  return tempp;
}

void check_queue(node *listpointer) {
  if (listpointer == NULL) {
    printf ("queue is empty!\n");
  } else {
    while (listpointer != NULL) {
      if(time(NULL) > listpointer->execute_at) {
        execute_command(listpointer->command);
        delayed = remove_item(listpointer);
      }
      listpointer = listpointer->next;
    }
  }
}

int help() {
  printf("Options are:\n");
  printf("-h; --help:\t\t Display this help\n");
  printf("-s; --show-keys:\t Show the key codes\n");
  exit(0);
}
