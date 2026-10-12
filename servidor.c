#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>

#define MAX_MSG 250
#define PUERTO 2026
#define MAX_CLIENTES 10

#define MAX_USUARIO 50

// Fichero con los usuarios
#define FICHERO_USUARIOS "usuarios.txt"

// Definimos los estados en los que puede estar un cliente
#define EST_CONECTADO 0     // conectado, sin identificar
#define EST_USUARIO_OK 1    // usuario correcto, falta la contraseña
#define EST_VALIDADO 2      // identificado (usuario y contraseña correctos)
#define EST_ESPERANDO 3     // esperando rival para jugar
#define EST_JUGANDO 4       // jugando una partida

typedef struct{
    int socket;
    int estado;
    char usuario[MAX_USUARIO];
    int partida;
} Cliente;

// Maximo de partidas a la vez
#define MAX_PARTIDAS 4

typedef struct{
    int activa;         // 1 si la partida está jugandose, 0 si no está activa
    int jugador1;       // socket del jugador 1 
    int jugador2;       // socket del jugador 2
} Partida;

void salirCliente(int socket, fd_set * readfds, int * numClientes, Cliente arrayClientes[], Partida partidas[]);
int buscarCliente(int socket, Cliente arrayClientes[], int numClientes);
void enviarMensaje(int socket, const char * texto);
void procesarMensaje(Cliente * cliente, char buffer[], Cliente arrayClientes[], int numClientes, Partida partidas[]);
int comprobarUsuario(char usuario[], char password[]);
int registrarUsuario(char usuario[], char password[]);
int buscarEnEspera(Cliente arrayClientes[], int numClientes);
int buscarPartidaLibre(Partida partidas[]);

int main(){
    int sd, new_sd;
    
    
    fd_set readfds, auxfds;
    int salida;
    Cliente arrayClientes[MAX_CLIENTES];
    Partida partidas[MAX_PARTIDAS];
    int numClientes = 0;
    int i;
    int on = 1;
    int recibidos;
    int pos;
    
    struct sockaddr_in sockname, from;
    socklen_t from_len;
    char buffer[MAX_MSG];

    // Abrimos el socket
    sd = socket(AF_INET, SOCK_STREAM, 0);
    if(sd == -1){
        perror("No se puede abrir el socket servidor");
        exit(1);
    }

    if(setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1){
        perror("Error en la operación setsockopt");
        exit(1);
    }

    sockname.sin_family = AF_INET;
    sockname.sin_port = htons(PUERTO);
    sockname.sin_addr.s_addr = INADDR_ANY;

    // Asociamos el socket a la dirección del servidor (a partir de aquí todo lo que llegue al puerto 2026 se enviará a este socket)
    if(bind(sd, (struct sockaddr *)&sockname, sizeof(sockname)) == -1){
        perror("Error en la operación bind\n");
        exit(1);
    }

    from_len = sizeof(from);

    if(listen(sd, 1) == -1){
        perror("Error en la operación listen");
        exit(1);
    }


    printf("Servidor esperando conexiones...\n");

    FD_ZERO(&readfds);
    FD_ZERO(&auxfds);
    FD_SET(sd, &readfds);

    bzero(partidas, sizeof(partidas));

    while(1){

        auxfds = readfds;

        salida = select(FD_SETSIZE, &auxfds, NULL, NULL, NULL);

        if(salida > 0){
            for(i = 0; i < FD_SETSIZE; i++){

                if(FD_ISSET(i, &auxfds)){

                    if(i == sd){
                        // Aquí van tus comentarios sobre accept
                        if((new_sd = accept(sd, (struct sockaddr *)&from, &from_len)) == -1){
                            perror("Error en la operación accept");
                        }
                        else{
                            printf("Cliente conectado desde %s:%d\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));

                            if(numClientes < MAX_CLIENTES){
                                arrayClientes[numClientes].socket = new_sd;
                                arrayClientes[numClientes].estado = EST_CONECTADO;
                                arrayClientes[numClientes].partida = -1;
                                bzero(arrayClientes[numClientes].usuario, MAX_USUARIO);
                                numClientes++;
                                FD_SET(new_sd, &readfds);

                                enviarMensaje(new_sd, "+Ok. Usuario conectado");
                            }
                            else{
                                enviarMensaje(new_sd, "-Err. Demasiados clientes conectados");
                                close(new_sd);
                            }
                        }
                    }
                    else{
                        // Un cliente ya conectado ha enviado algo, o se ha desconectado
                        bzero(buffer, sizeof(buffer));

                        recibidos = recv(i, buffer, sizeof(buffer), 0);

                        if(recibidos > 0){
                            buffer[strcspn(buffer, "\n")] = '\0';

                            if(strcmp(buffer, "SALIR") == 0){
                                salirCliente(i, &readfds, &numClientes, arrayClientes, partidas);
                            }
                            else{
                                pos = buscarCliente(i, arrayClientes, numClientes);
                                if(pos != -1)
                                    procesarMensaje(&arrayClientes[pos], buffer, arrayClientes, numClientes, partidas);
                            }
                        }
                        else{
                            if(recibidos == -1)
                                perror("Error en la operación recv");
                            else
                                printf("El cliente %d se ha desconectado\n", i);

                            salirCliente(i, &readfds, &numClientes, arrayClientes, partidas);
                        }
                    }
                }
            }
        }
    }

    close(sd);
    return 0;
}

void salirCliente(int socket, fd_set * readfds, int * numClientes, Cliente arrayClientes[], Partida partidas[]){
    int j, pos, posRival, p, socketRival;

    pos = buscarCliente(socket, arrayClientes, *numClientes);

    // Si estaba jugando, se termina la partida y se avisa al rival
    if(pos != -1 && arrayClientes[pos].estado == EST_JUGANDO){
        p = arrayClientes[pos].partida;

        if(partidas[p].jugador1 == socket)
            socketRival = partidas[p].jugador2;
        else
            socketRival = partidas[p].jugador1;

        posRival = buscarCliente(socketRival, arrayClientes, *numClientes);

        if(posRival != -1){
            arrayClientes[posRival].estado = EST_VALIDADO;
            arrayClientes[posRival].partida = -1;
            enviarMensaje(socketRival, "+Ok. Tu oponente ha terminado la partida");
        }

        partidas[p].activa = 0;
    }

    close(socket);
    FD_CLR(socket, readfds);

    // Reestructurar el array de clientes
    for(j = 0; j < (*numClientes) - 1; j++)
        if(arrayClientes[j].socket == socket)
            break;
    for(; j < (*numClientes) - 1; j++)
        arrayClientes[j] = arrayClientes[j+1];

    (*numClientes)--;
}

// Devuelve la posición del cliente en el array, o -1 si no está
int buscarCliente(int socket, Cliente arrayClientes[], int numClientes){
    int j;

    for(j = 0; j < numClientes; j++){
        if(arrayClientes[j].socket == socket){
            return j;
        }
    }
    
    return -1;
   }

// Envía siempre un bloque de MAX_MSG bytes, como acordamos con el cliente
void enviarMensaje(int socket, const char * texto){
    char buffer[MAX_MSG];

    bzero(buffer, sizeof(buffer));
    strncpy(buffer, texto, sizeof(buffer) - 1);

    if(send(socket, buffer, sizeof(buffer), 0) == -1)
        perror("Error en la operación send");
}

void procesarMensaje(Cliente * cliente, char buffer[], Cliente arrayClientes[], int numClientes, Partida partidas[]){
    char comando[MAX_MSG], arg1[MAX_MSG], arg2[MAX_MSG], arg3[MAX_MSG];
    int campos, hueco, rival;

    bzero(comando, sizeof(comando));
    bzero(arg1, sizeof(arg1));
    bzero(arg2, sizeof(arg2));
    bzero(arg3, sizeof(arg3));

    // campos = cuántas palabras tiene el mensaje (comando + argumentos)
    campos = sscanf(buffer, "%s %s %s %s", comando, arg1, arg2, arg3);

    if(strcmp(comando, "USUARIO") == 0){

        if(cliente->estado != EST_CONECTADO){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        else if(campos != 2 || strlen(arg1) >= MAX_USUARIO){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: USUARIO [usuario]");
        }
        else if(comprobarUsuario(arg1, NULL)){
            strcpy(cliente->usuario, arg1);
            cliente->estado = EST_USUARIO_OK;
            enviarMensaje(cliente->socket, "+Ok. Usuario correcto");
        }
        else{
            enviarMensaje(cliente->socket, "-Err. Usuario incorrecto");
        }
    }

    else if(strcmp(comando, "PASSWORD") == 0){

        if(cliente->estado != EST_USUARIO_OK){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        else if(campos != 2){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: PASSWORD [password]");
        }
        else if(comprobarUsuario(cliente->usuario, arg1)){
            cliente->estado = EST_VALIDADO;
            enviarMensaje(cliente->socket, "+Ok. Usuario validado");
        }
        else{
            cliente->estado = EST_CONECTADO;
            enviarMensaje(cliente->socket, "-Err. Error en la validación");
        }
    }

    else if(strcmp(comando, "REGISTRO") == 0){

        if(cliente->estado != EST_CONECTADO){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        else if(campos != 3 || strlen(arg1) >= MAX_USUARIO || strlen(arg2) >= MAX_USUARIO){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: REGISTRO [usuario] [password]");
        }
        else if(comprobarUsuario(arg1, NULL)){
            enviarMensaje(cliente->socket, "-Err. El usuario ya existe");
        }
        else if(registrarUsuario(arg1, arg2)){
            enviarMensaje(cliente->socket, "+Ok. Usuario registrado");
        }
        else{
            enviarMensaje(cliente->socket, "-Err. No se ha podido registrar el usuario");
        }
    }

    else if(strcmp(comando, "INICIAR-PARTIDA") == 0){

        if(cliente->estado != EST_VALIDADO){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        else if(campos != 1){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: INICIAR-PARTIDA");
        }
        else{
            hueco = buscarPartidaLibre(partidas);

            if(hueco == -1){
                enviarMensaje(cliente->socket, "-Err. No hay partidas disponibles, inténtalo más tarde");
            }
            else{
                rival = buscarEnEspera(arrayClientes, numClientes);

                if(rival == -1){
                    // Nadie esperaba: este cliente se queda esperando
                    cliente->estado = EST_ESPERANDO;
                    enviarMensaje(cliente->socket, "+Ok. Esperando otro jugador");
                }
                else{
                    // Ya había uno esperando: se forma la pareja
                    partidas[hueco].activa = 1;
                    partidas[hueco].jugador1 = arrayClientes[rival].socket;
                    partidas[hueco].jugador2 = cliente->socket;

                    arrayClientes[rival].estado = EST_JUGANDO;
                    arrayClientes[rival].partida = hueco;
                    cliente->estado = EST_JUGANDO;
                    cliente->partida = hueco;

                    enviarMensaje(arrayClientes[rival].socket, "+Ok. Empieza la partida");
                    enviarMensaje(cliente->socket, "+Ok. Empieza la partida");
                }
            }
        }
    }

    // Aquí irá INICIAR-PARTIDA...

    else{
        enviarMensaje(cliente->socket, "-Err. Comando desconocido");
    }
}

// Busca el usuario en el fichero. Si password es NULL, solo comprueba que
// el usuario exista; si no, comprueba que usuario y contraseña coincidan.
int comprobarUsuario(char usuario[], char password[]){
    FILE * f;
    char u[MAX_USUARIO], p[MAX_USUARIO];
    int encontrado = 0;

    f = fopen(FICHERO_USUARIOS, "r");
    if(f == NULL){
        return 0;
    }
           
    while(fscanf(f, "%49s %49s", u, p) == 2){
        if(strcmp(u, usuario) == 0 && (password == NULL || strcmp(p, password) == 0)){
            encontrado = 1;
            break;
        }
    }

    fclose(f);
    return encontrado;
}

// Añade un usuario al final del fichero. Devuelve 1 si lo consigue, 0 si falla.
int registrarUsuario(char usuario[], char password[]){
    FILE * f;

    f = fopen(FICHERO_USUARIOS, "a");
    if(f == NULL){
        perror("Error al abrir el fichero de usuarios");
        return 0;
    }

    fprintf(f, "%s %s\n", usuario, password);
    fclose(f);

    return 1;
}  

// Devuelve la posición del cliente que espera rival, o -1 si no hay ninguno
int buscarEnEspera(Cliente arrayClientes[], int numClientes){
    int j;

    for(j = 0; j < numClientes; j++)
        if(arrayClientes[j].estado == EST_ESPERANDO)
            return j;

    return -1;
}

// Devuelve el primer hueco libre del array de partidas, o -1 si están las 4 ocupadas
int buscarPartidaLibre(Partida partidas[]){
    int j;

    for(j = 0; j < MAX_PARTIDAS; j++)
        if(partidas[j].activa == 0)
            return j;

    return -1;
}