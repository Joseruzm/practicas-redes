// ============================================================================
//  CABECERAS
// ============================================================================
#include <stdio.h>          // printf, perror, fopen, fclose, fscanf, fprintf, sscanf
#include <sys/types.h>      // tipos básicos del sistema (socklen_t, etc.)
#include <sys/socket.h>     // socket, bind, listen, accept, send, recv, setsockopt
#include <netinet/in.h>     // struct sockaddr_in, htons, INADDR_ANY
#include <arpa/inet.h>      // inet_ntoa (pasa una IP binaria a texto "127.0.0.1")
#include <netdb.h>          // funciones de red (nombres de host...). Aquí no se usa nada concreto
#include <stdlib.h>         // exit
#include <string.h>         // strcmp, strcpy, strncpy, strlen, strcspn, bzero
#include <unistd.h>         // close
#include <sys/select.h>     // select, fd_set, FD_ZERO, FD_SET, FD_ISSET, FD_CLR, FD_SETSIZE

// ============================================================================
//  CONSTANTES
// ============================================================================

// Tamaño de TODOS los mensajes que se envían/reciben (el enunciado dice máx. 200 caracteres,
// usamos 250 para tener margen y el '\0' final)
#define MAX_MSG 250
// Puerto en el que escucha el servidor (lo exige el enunciado)
#define PUERTO 2026
// Máximo de clientes conectados a la vez (lo exige el enunciado)
#define MAX_CLIENTES 10

// Tamaño máximo del nombre de usuario (y también de la contraseña) incluido el '\0'
#define MAX_USUARIO 50

// Fichero con los usuarios
// (cada línea tiene el formato: "usuario password")
#define FICHERO_USUARIOS "usuarios.txt"

// Definimos los estados en los que puede estar un cliente
// El estado es lo que permite al servidor saber qué comandos son válidos en cada momento
#define EST_CONECTADO 0     // conectado, sin identificar
#define EST_USUARIO_OK 1    // usuario correcto, falta la contraseña
#define EST_VALIDADO 2      // identificado (usuario y contraseña correctos)
#define EST_ESPERANDO 3     // esperando rival para jugar
#define EST_JUGANDO 4       // jugando una partida

// ============================================================================
//  ESTRUCTURAS DE DATOS
// ============================================================================

// Toda la información que el servidor guarda de cada cliente conectado
typedef struct{
    int socket;                 // descriptor del socket con el que nos comunicamos con él
    int estado;                 // uno de los EST_* de arriba
    char usuario[MAX_USUARIO];  // nombre con el que se ha identificado
    int partida;                // índice de la partida en la que juega (-1 si no está en ninguna)
} Cliente;

// Maximo de partidas a la vez
#define MAX_PARTIDAS 4

// Información de cada partida
typedef struct{
    int activa;         // 1 si la partida está jugandose, 0 si no está activa
    int jugador1;       // socket del jugador 1 
    int jugador2;       // socket del jugador 2
} Partida;

// ============================================================================
//  PROTOTIPOS (se declaran aquí para poder usar las funciones en el main
//  aunque estén definidas más abajo en el fichero)
// ============================================================================

// Elimina a un cliente (cierra su socket, lo saca del array y termina su partida si estaba jugando)
void salirCliente(int socket, fd_set * readfds, int * numClientes, Cliente arrayClientes[], Partida partidas[]);
// Devuelve en qué posición del array está el cliente con ese socket
int buscarCliente(int socket, Cliente arrayClientes[], int numClientes);
// Envía un mensaje de texto a un cliente
void enviarMensaje(int socket, const char * texto);
// Interpreta el mensaje de un cliente y actúa según el comando y el estado del cliente
void procesarMensaje(Cliente * cliente, char buffer[], Cliente arrayClientes[], int numClientes, Partida partidas[]);
// Comprueba si un usuario (y opcionalmente su contraseña) está en el fichero
int comprobarUsuario(char usuario[], char password[]);
// Añade un usuario nuevo al fichero
int registrarUsuario(char usuario[], char password[]);
// Busca un cliente que esté esperando rival
int buscarEnEspera(Cliente arrayClientes[], int numClientes);
// Busca un hueco libre en el array de partidas
int buscarPartidaLibre(Partida partidas[]);

// ============================================================================
//  MAIN
// ============================================================================
int main(){
    // sd = socket de escucha (el que recibe las conexiones nuevas)
    // new_sd = socket nuevo que devuelve accept para hablar con un cliente concreto
    int sd, new_sd;
    
    
    // readfds = conjunto de sockets que vigilamos (el original, que mantenemos siempre actualizado)
    // auxfds = copia de readfds que se pasa a select, porque select LO MODIFICA
    fd_set readfds, auxfds;
    // Valor que devuelve select (nº de descriptores con actividad)
    int salida;
    // Array con todos los clientes conectados
    Cliente arrayClientes[MAX_CLIENTES];
    // Array con las 4 partidas posibles
    Partida partidas[MAX_PARTIDAS];
    // Cuántos clientes hay conectados ahora mismo (las posiciones 0..numClientes-1 están ocupadas)
    int numClientes = 0;
    // Contador para recorrer los descriptores
    int i;
    // Valor para activar la opción SO_REUSEADDR (1 = activada)
    int on = 1;
    // Bytes que devuelve recv
    int recibidos;
    // Posición de un cliente en el array
    int pos;
    
    // sockname = dirección del servidor; from = dirección del cliente que se conecta
    struct sockaddr_in sockname, from;
    // Tamaño de la estructura from (accept lo necesita y lo rellena)
    socklen_t from_len;
    // Buffer donde se guarda lo que recibimos de los clientes
    char buffer[MAX_MSG];

    // Abrimos el socket
    // AF_INET = IPv4, SOCK_STREAM = TCP, 0 = el sistema elige el protocolo
    sd = socket(AF_INET, SOCK_STREAM, 0);
    // socket devuelve -1 si falla
    if(sd == -1){
        perror("No se puede abrir el socket servidor");
        exit(1);
    }

    // SO_REUSEADDR permite volver a lanzar el servidor justo después de cerrarlo
    // sin tener que esperar a que el puerto salga del estado TIME_WAIT
    if(setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1){
        perror("Error en la operación setsockopt");
        exit(1);
    }

    // Rellenamos la dirección del servidor:
    // familia IPv4
    sockname.sin_family = AF_INET;
    // puerto 2026; htons pasa el número al orden de bytes de red (big endian)
    sockname.sin_port = htons(PUERTO);
    // INADDR_ANY = aceptamos conexiones por cualquier interfaz de red de la máquina
    sockname.sin_addr.s_addr = INADDR_ANY;

    // Asociamos el socket a la dirección del servidor (a partir de aquí todo lo que llegue al puerto 2026 se enviará a este socket)
    if(bind(sd, (struct sockaddr *)&sockname, sizeof(sockname)) == -1){
        perror("Error en la operación bind\n");
        exit(1);
    }

    // Guardamos el tamaño de la estructura from, que accept necesita como parámetro
    from_len = sizeof(from);

    // Ponemos el socket en modo escucha (pasivo). El 2º parámetro es el tamaño
    // de la cola de conexiones pendientes de aceptar
    if(listen(sd, 1) == -1){
        perror("Error en la operación listen");
        exit(1);
    }


    printf("Servidor esperando conexiones...\n");

    // Inicializamos los dos conjuntos de descriptores a vacío
    FD_ZERO(&readfds);
    FD_ZERO(&auxfds);
    // Añadimos el socket de escucha al conjunto: así select nos avisará cuando llegue una conexión nueva
    FD_SET(sd, &readfds);

    // Ponemos a 0 todo el array de partidas, así activa = 0 en todas (todas libres).
    // sizeof(partidas) vale el tamaño de TODO el array porque está declarado en esta función
    bzero(partidas, sizeof(partidas));

    // ------------------------------------------------------------------------
    //  BUCLE PRINCIPAL DEL SERVIDOR (no termina nunca)
    // ------------------------------------------------------------------------
    while(1){

        // Copiamos el conjunto original, porque select va a modificar auxfds
        // dejando solo los descriptores que tienen actividad
        auxfds = readfds;

        // select bloquea (timeout NULL) hasta que haya actividad en algún socket del conjunto de lectura.
        // FD_SETSIZE = nº máximo de descriptores que puede vigilar select
        salida = select(FD_SETSIZE, &auxfds, NULL, NULL, NULL);

        // Si select devuelve > 0, hay al menos un socket con actividad
        if(salida > 0){
            // Recorremos todos los descriptores posibles para ver cuáles tienen actividad
            for(i = 0; i < FD_SETSIZE; i++){

                // ¿El descriptor i tiene actividad?
                if(FD_ISSET(i, &auxfds)){

                    // ---- CASO 1: actividad en el socket de escucha = cliente nuevo ----
                    if(i == sd){
                        // Aquí van tus comentarios sobre accept
                        // accept devuelve un socket nuevo para hablar con ese cliente
                        // y rellena from con su IP y puerto
                        if((new_sd = accept(sd, (struct sockaddr *)&from, &from_len)) == -1){
                            perror("Error en la operación accept");
                        }
                        else{
                            // Mostramos por pantalla la IP y puerto del cliente (ntohs pasa el puerto a orden de host)
                            printf("Cliente conectado desde %s:%d\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));

                            // Si hay sitio, lo guardamos en el array
                            if(numClientes < MAX_CLIENTES){
                                // Guardamos su socket en la primera posición libre
                                arrayClientes[numClientes].socket = new_sd;
                                // Al conectarse aún no se ha identificado
                                arrayClientes[numClientes].estado = EST_CONECTADO;
                                // Todavía no está en ninguna partida
                                arrayClientes[numClientes].partida = -1;
                                // Dejamos el nombre de usuario vacío
                                bzero(arrayClientes[numClientes].usuario, MAX_USUARIO);
                                // Ahora hay un cliente más
                                numClientes++;
                                // Lo añadimos al conjunto vigilado por select para enterarnos cuando envíe algo
                                FD_SET(new_sd, &readfds);

                                // Mensaje de bienvenida que pide el enunciado
                                enviarMensaje(new_sd, "+Ok. Usuario conectado");
                            }
                            // Si ya hay 10 clientes, lo rechazamos
                            else{
                                enviarMensaje(new_sd, "-Err. Demasiados clientes conectados");
                                // Cerramos su socket. No lo hemos añadido a readfds, así que no hace falta FD_CLR
                                close(new_sd);
                            }
                        }
                    }
                    // ---- CASO 2: actividad en el socket de un cliente ya conectado ----
                    else{
                        // Un cliente ya conectado ha enviado algo, o se ha desconectado
                        // Limpiamos el buffer para que no queden restos de mensajes anteriores
                        bzero(buffer, sizeof(buffer));

                        // Leemos lo que ha enviado el cliente i. Devuelve:
                        //   > 0  -> nº de bytes recibidos
                        //   == 0 -> el cliente ha cerrado la conexión
                        //   == -1 -> error
                        recibidos = recv(i, buffer, sizeof(buffer), 0);

                        // Hemos recibido datos
                        if(recibidos > 0){
                            // Quitamos el salto de línea del final (lo pone fgets en el cliente).
                            // strcspn devuelve la posición del primer '\n' y ahí ponemos el fin de cadena
                            buffer[strcspn(buffer, "\n")] = '\0';

                            // SALIR se trata aquí fuera porque es válido en cualquier estado
                            if(strcmp(buffer, "SALIR") == 0){
                                salirCliente(i, &readfds, &numClientes, arrayClientes, partidas);
                            }
                            // Cualquier otro mensaje se procesa según el estado del cliente
                            else{
                                // Buscamos al cliente en el array a partir de su socket
                                pos = buscarCliente(i, arrayClientes, numClientes);
                                // Si lo hemos encontrado, procesamos su mensaje
                                if(pos != -1)
                                    procesarMensaje(&arrayClientes[pos], buffer, arrayClientes, numClientes, partidas);
                            }
                        }
                        // recv devolvió 0 o -1: el cliente se ha ido (o hubo un error)
                        else{
                            if(recibidos == -1)
                                perror("Error en la operación recv");
                            else
                                printf("El cliente %d se ha desconectado\n", i);

                            // En ambos casos lo eliminamos del servidor
                            salirCliente(i, &readfds, &numClientes, arrayClientes, partidas);
                        }
                    }
                }
            }
        }
    }

    // (no se llega nunca aquí porque el while(1) no termina)
    close(sd);
    return 0;
}

// ============================================================================
//  salirCliente: elimina a un cliente del servidor
//  - Si estaba jugando, termina la partida y avisa al rival
//  - Cierra su socket y lo quita de select y del array de clientes
//  Se usa tanto con el comando SALIR como cuando el cliente se desconecta de golpe
// ============================================================================
void salirCliente(int socket, fd_set * readfds, int * numClientes, Cliente arrayClientes[], Partida partidas[]){
    // pos = posición del cliente que se va; posRival = posición de su rival
    // p = índice de la partida; socketRival = socket del rival
    int j, pos, posRival, p, socketRival;

    // Localizamos al cliente en el array
    pos = buscarCliente(socket, arrayClientes, *numClientes);

    // Si estaba jugando, se termina la partida y se avisa al rival
    if(pos != -1 && arrayClientes[pos].estado == EST_JUGANDO){
        // Índice de la partida en la que estaba
        p = arrayClientes[pos].partida;

        // Averiguamos cuál de los dos jugadores es el rival: el que NO es el que se va
        if(partidas[p].jugador1 == socket)
            socketRival = partidas[p].jugador2;
        else
            socketRival = partidas[p].jugador1;

        // Buscamos al rival en el array de clientes
        posRival = buscarCliente(socketRival, arrayClientes, *numClientes);

        // Si el rival sigue conectado, lo devolvemos al estado "validado"
        // (puede volver a pedir otra partida) y le avisamos
        if(posRival != -1){
            arrayClientes[posRival].estado = EST_VALIDADO;
            arrayClientes[posRival].partida = -1;
            enviarMensaje(socketRival, "+Ok. Tu oponente ha terminado la partida");
        }

        // Marcamos la partida como libre para que se pueda reutilizar
        partidas[p].activa = 0;
    }

    // Cerramos el socket del cliente que se va
    close(socket);
    // Lo quitamos del conjunto que vigila select
    FD_CLR(socket, readfds);

    // Reestructurar el array de clientes
    // 1º bucle: avanzamos j hasta la posición del cliente que se va
    for(j = 0; j < (*numClientes) - 1; j++)
        if(arrayClientes[j].socket == socket)
            break;
    // 2º bucle: desde esa posición, movemos cada cliente una posición a la izquierda
    // para tapar el hueco (si era el último no hace nada)
    for(; j < (*numClientes) - 1; j++)
        arrayClientes[j] = arrayClientes[j+1];

    // Hay un cliente menos
    (*numClientes)--;
}

// ============================================================================
//  buscarCliente: devuelve la posición del cliente en el array a partir de su socket
// ============================================================================
// Devuelve la posición del cliente en el array, o -1 si no está
int buscarCliente(int socket, Cliente arrayClientes[], int numClientes){
    int j;

    // Recorremos solo las posiciones ocupadas
    for(j = 0; j < numClientes; j++){
        // Si el socket coincide, devolvemos su posición
        if(arrayClientes[j].socket == socket){
            return j;
        }
    }
    
    // Si hemos llegado aquí, no estaba
    return -1;
   }

// ============================================================================
//  enviarMensaje: envía un texto al cliente
// ============================================================================
// Envía siempre un bloque de MAX_MSG bytes, como acordamos con el cliente
void enviarMensaje(int socket, const char * texto){
    char buffer[MAX_MSG];

    // Dejamos todo el buffer a ceros (así lo que sobra tras el texto son '\0')
    bzero(buffer, sizeof(buffer));
    // Copiamos el texto, como mucho MAX_MSG-1 caracteres para que siempre quede el '\0' final
    strncpy(buffer, texto, sizeof(buffer) - 1);

    // Enviamos el buffer completo (MAX_MSG bytes). send devuelve -1 si falla
    if(send(socket, buffer, sizeof(buffer), 0) == -1)
        perror("Error en la operación send");
}

// ============================================================================
//  procesarMensaje: núcleo del protocolo.
//  Recibe el mensaje ya sin '\n', lo trocea en palabras y, según el comando
//  y el ESTADO del cliente, decide qué hacer y qué responder.
// ============================================================================
void procesarMensaje(Cliente * cliente, char buffer[], Cliente arrayClientes[], int numClientes, Partida partidas[]){
    // comando = primera palabra del mensaje; arg1, arg2, arg3 = las siguientes palabras
    char comando[MAX_MSG], arg1[MAX_MSG], arg2[MAX_MSG], arg3[MAX_MSG];
    // campos = nº de palabras del mensaje
    // hueco = índice de partida libre; rival = posición en el array del cliente que esperaba
    int campos, hueco, rival;

    // Limpiamos las cadenas para que no tengan basura
    bzero(comando, sizeof(comando));
    bzero(arg1, sizeof(arg1));
    bzero(arg2, sizeof(arg2));
    bzero(arg3, sizeof(arg3));

    // campos = cuántas palabras tiene el mensaje (comando + argumentos)
    // sscanf con %s separa por espacios y devuelve cuántas ha podido leer.
    // Así "REGISTRO ana 1234" -> comando="REGISTRO", arg1="ana", arg2="1234", campos=3
    campos = sscanf(buffer, "%s %s %s %s", comando, arg1, arg2, arg3);

    // ---------------------------------------------------------------- USUARIO
    if(strcmp(comando, "USUARIO") == 0){

        // Solo es válido si aún no se ha identificado (estado CONECTADO)
        if(cliente->estado != EST_CONECTADO){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        // Debe tener exactamente 2 palabras (USUARIO + nombre) y el nombre debe caber en el array
        else if(campos != 2 || strlen(arg1) >= MAX_USUARIO){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: USUARIO [usuario]");
        }
        // Con password NULL solo se comprueba que el usuario exista en el fichero
        else if(comprobarUsuario(arg1, NULL)){
            // Guardamos el nombre para usarlo cuando llegue el PASSWORD
            strcpy(cliente->usuario, arg1);
            // Pasamos al estado "falta la contraseña"
            cliente->estado = EST_USUARIO_OK;
            enviarMensaje(cliente->socket, "+Ok. Usuario correcto");
        }
        // El usuario no existe
        else{
            enviarMensaje(cliente->socket, "-Err. Usuario incorrecto");
        }
    }

    // --------------------------------------------------------------- PASSWORD
    else if(strcmp(comando, "PASSWORD") == 0){

        // Solo es válido justo después de un USUARIO correcto
        if(cliente->estado != EST_USUARIO_OK){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        // Debe tener exactamente 2 palabras (PASSWORD + contraseña)
        else if(campos != 2){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: PASSWORD [password]");
        }
        // Comprobamos que el usuario guardado y esta contraseña coinciden en el fichero
        else if(comprobarUsuario(cliente->usuario, arg1)){
            // Ya está identificado: puede iniciar partidas
            cliente->estado = EST_VALIDADO;
            enviarMensaje(cliente->socket, "+Ok. Usuario validado");
        }
        // Contraseña incorrecta
        else{
            // Volvemos al principio: tendrá que mandar USUARIO otra vez
            cliente->estado = EST_CONECTADO;
            enviarMensaje(cliente->socket, "-Err. Error en la validación");
        }
    }

    // --------------------------------------------------------------- REGISTRO
    else if(strcmp(comando, "REGISTRO") == 0){

        // Solo se puede registrar quien aún no se ha identificado
        if(cliente->estado != EST_CONECTADO){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        // Debe tener 3 palabras (REGISTRO + usuario + password) y ambos deben caber en MAX_USUARIO
        else if(campos != 3 || strlen(arg1) >= MAX_USUARIO || strlen(arg2) >= MAX_USUARIO){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: REGISTRO [usuario] [password]");
        }
        // Control de colisiones: si el usuario ya existe en el fichero, error
        else if(comprobarUsuario(arg1, NULL)){
            enviarMensaje(cliente->socket, "-Err. El usuario ya existe");
        }
        // Si no existe, lo añadimos al fichero
        else if(registrarUsuario(arg1, arg2)){
            enviarMensaje(cliente->socket, "+Ok. Usuario registrado");
        }
        // No se pudo escribir en el fichero
        else{
            enviarMensaje(cliente->socket, "-Err. No se ha podido registrar el usuario");
        }
    }

    // ------------------------------------------------------- INICIAR-PARTIDA
    else if(strcmp(comando, "INICIAR-PARTIDA") == 0){

        // Solo puede iniciar partida quien está validado (no si ya espera o ya juega)
        if(cliente->estado != EST_VALIDADO){
            enviarMensaje(cliente->socket, "-Err. Comando no válido en este momento");
        }
        // El comando no lleva argumentos: solo 1 palabra
        else if(campos != 1){
            enviarMensaje(cliente->socket, "-Err. Formato incorrecto. Uso: INICIAR-PARTIDA");
        }
        else{
            // Buscamos un hueco libre en el array de partidas (-1 si están las 4 ocupadas)
            hueco = buscarPartidaLibre(partidas);

            // Límite de 4 partidas simultáneas
            if(hueco == -1){
                enviarMensaje(cliente->socket, "-Err. No hay partidas disponibles, inténtalo más tarde");
            }
            else{
                // Miramos si ya hay alguien esperando rival
                rival = buscarEnEspera(arrayClientes, numClientes);

                if(rival == -1){
                    // Nadie esperaba: este cliente se queda esperando
                    cliente->estado = EST_ESPERANDO;
                    enviarMensaje(cliente->socket, "+Ok. Esperando otro jugador");
                }
                else{
                    // Ya había uno esperando: se forma la pareja
                    // Rellenamos la partida: el que esperaba es el jugador 1 y el que acaba de llegar el jugador 2
                    partidas[hueco].activa = 1;
                    partidas[hueco].jugador1 = arrayClientes[rival].socket;
                    partidas[hueco].jugador2 = cliente->socket;

                    // Actualizamos el estado y la partida de los dos jugadores
                    arrayClientes[rival].estado = EST_JUGANDO;
                    arrayClientes[rival].partida = hueco;
                    cliente->estado = EST_JUGANDO;
                    cliente->partida = hueco;

                    // Avisamos a ambos de que empieza la partida
                    enviarMensaje(arrayClientes[rival].socket, "+Ok. Empieza la partida");
                    enviarMensaje(cliente->socket, "+Ok. Empieza la partida");
                }
            }
        }
    }

    // Aquí irá INICIAR-PARTIDA...

    // ------------------------------------------- Cualquier otra cosa: error
    else{
        enviarMensaje(cliente->socket, "-Err. Comando desconocido");
    }
}

// ============================================================================
//  comprobarUsuario: consulta el fichero de usuarios
// ============================================================================
// Busca el usuario en el fichero. Si password es NULL, solo comprueba que
// el usuario exista; si no, comprueba que usuario y contraseña coincidan.
int comprobarUsuario(char usuario[], char password[]){
    // Puntero al fichero
    FILE * f;
    // u y p = usuario y contraseña de la línea que se está leyendo del fichero
    char u[MAX_USUARIO], p[MAX_USUARIO];
    // Resultado: 1 si se encuentra, 0 si no
    int encontrado = 0;

    // Abrimos el fichero en modo lectura
    f = fopen(FICHERO_USUARIOS, "r");
    // Si no existe (por ejemplo, aún no se ha registrado nadie), no hay usuarios
    if(f == NULL){
        return 0;
    }
           
    // Leemos pares "usuario password" mientras fscanf consiga leer los 2 campos.
    // %49s = máximo 49 caracteres (MAX_USUARIO-1) para no desbordar u ni p
    while(fscanf(f, "%49s %49s", u, p) == 2){
        // Coincide si el usuario es igual Y (no nos piden comprobar contraseña O la contraseña también es igual)
        if(strcmp(u, usuario) == 0 && (password == NULL || strcmp(p, password) == 0)){
            encontrado = 1;
            // Ya lo hemos encontrado, no hace falta seguir leyendo
            break;
        }
    }

    // Cerramos el fichero siempre
    fclose(f);
    return encontrado;
}

// ============================================================================
//  registrarUsuario: añade un usuario al fichero
// ============================================================================
// Añade un usuario al final del fichero. Devuelve 1 si lo consigue, 0 si falla.
int registrarUsuario(char usuario[], char password[]){
    FILE * f;

    // Modo "a" (append): escribe al final y crea el fichero si no existe
    f = fopen(FICHERO_USUARIOS, "a");
    if(f == NULL){
        perror("Error al abrir el fichero de usuarios");
        return 0;
    }

    // Escribimos una línea con el formato "usuario password"
    fprintf(f, "%s %s\n", usuario, password);
    // Cerrar el fichero también vuelca lo escrito al disco
    fclose(f);

    return 1;
}  

// ============================================================================
//  buscarEnEspera: localiza a un jugador que espera rival
// ============================================================================
// Devuelve la posición del cliente que espera rival, o -1 si no hay ninguno
int buscarEnEspera(Cliente arrayClientes[], int numClientes){
    int j;

    // Recorremos los clientes conectados
    for(j = 0; j < numClientes; j++)
        // El primero que esté en estado ESPERANDO es el elegido
        if(arrayClientes[j].estado == EST_ESPERANDO)
            return j;

    // Nadie esperando
    return -1;
}

// ============================================================================
//  buscarPartidaLibre: localiza un hueco en el array de partidas
// ============================================================================
// Devuelve el primer hueco libre del array de partidas, o -1 si están las 4 ocupadas
int buscarPartidaLibre(Partida partidas[]){
    int j;

    // Recorremos las 4 posibles partidas
    for(j = 0; j < MAX_PARTIDAS; j++)
        // La primera con activa == 0 está libre
        if(partidas[j].activa == 0)
            return j;

    // Todas ocupadas
    return -1;
}