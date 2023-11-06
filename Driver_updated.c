#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/cred.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>


#define BUFF_SIZE 64
#define LOCALBUFF_SIZE 4
#define NB_PORT 2

#define BAUDRATE_MIN 50
#define BAUDRATE_MAX 115200

#define DATA_SIZE_MIN 5
#define DATA_SIZE_MAX 8

#define PARITY_NONE 0
#define PARITY_ODD 1
#define PARITY_EVEN 2



MODULE_AUTHOR("Baptiste & Loïs");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Driver");

int MyModule_X = 5;

//déclarations de fonctions
static int MyModule_open(struct inode *inode, struct file *filp);
static int MyModule_release(struct inode *inode, struct file *filp);
static ssize_t MyModule_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
static ssize_t MyModule_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
static int MyModule_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);


module_param(MyModule_X, int, S_IRUGO);

EXPORT_SYMBOL_GPL(MyModule_X);  //permet de rendre la variable locale Module_X visible par les autres modules

dev_t My_dev;  //variable globale pour le major et le minor
struct class *MyClass;  //variable globale pour la classe
struct cdev My_cdev;

struct file_operations MyModule_fops = {   //le nom "MyModule" peut partout être remplacé par "pilote"
	.owner		=	THIS_MODULE,
	.read		=	MyModule_read,
	.write		=	MyModule_write,
	.open		=	MyModule_open,
	.release	=	MyModule_release,
    .unlocked_ioctl = MyModule_ioctl
};

struct circular_buffer{
    char *Buffer;
    int buff_size;
    int empty;
    int full;
    int head;
    int tail;
}

struct perso{
    int read; 
    int write;
    int uid;
    struct circular_buffer *circular_buffer;
    int bytes_count;
    int bytes_written;
    int bytes_read;
    int already_open;
    //déclaration sémaphore
    struct semaphore MySem;
    //déclaration spinlock
    spinlock_t MySpin;
    //déclaration de la queue de tâches
    wait_queue_head_t RdQ;
    wait_queue_head_t WrQ;
};
struct perso perso[NB_PORT];


//cela équivaut à faire struct StructurePilote *Pilote; 
//et après il faudrait l'allouer avec kmalloc(sizeof(struct StructurePilote), GFP_KERNEL); dans le init


static int __init mod_init (void) {
    int n;
    alloc_chrdev_region (&My_dev,  0,  NB_PORT, "Serial driver");  //allocation dynamique du major et du minor (numéro d'unité-matériel)

    //les noeuds dus unités-matériels sont créés dans au chargement du module et détruits au déchargement du module
    //MyClass = class_create(THIS_MODULE, "MyModule");  //création de la classe
    MyClass = class_create(THIS_MODULE, "Driver");  //création de la classe
    
    //initialisation de la structure cdev

    cdev_init(&My_cdev, &MyModule_fops); //initialisation de la structure cdev pour le pilote

    for (n = 0; n < NB_PORT; n++){
        //création des noeuds dans /dev
        //initialisation des variables de la structure perso
        perso[n].user=0;
        perso[n].read=0;
        perso[n].write=0;
        perso[n].bytes_count=0;
        perso[n].bytes_written=0;
        perso[n].bytes_read=0;
        perso[n].already_open=0;
    	device_create(MyClass, NULL, (My_dev + n), NULL, "MyModuleNode%d", n);
        sema_init(&perso[n].MySem, 1);
        spin_lock_init(&perso[n].MySpin);
        init_waitqueue_head(&perso[n].RdQ);
        init_waitqueue_head(&perso[n].WrQ);
    }

    //A FAIRE EN TOUT DERNIER 
    cdev_add(&My_cdev, My_dev, NB_PORT); 

    printk(KERN_WARNING"Big Driver : Hello World ! Serial driver = %u\n", MyModule_X);

    return 0;
}

static void __exit mod_exit (void) {
    int n;
    cdev_del(&My_cdev); //suppression de la structure cdev

    for (n = 0; n < NB_PORT; n++) {
    	device_destroy(MyClass, (My_dev + n));  //destruction des noeuds
        //TODO : FREE BUFFER
        if(perso[n].circular_buffer.Buffer!=NULL){
            kfree(perso[n].circular_buffer.Buffer);
        }
    }
    class_destroy(MyClass);

    unregister_chrdev_region(My_dev, NB_PORT);

    //ici on pourrait faire un kfree(Pilote) si on avait fait un kmalloc(Pilote) dans le init

    printk(KERN_WARNING"Big Driver : Goodbye cruel World !\n");
}   

module_init(mod_init);
module_exit(mod_exit);

static int MyModule_open(struct inode *inode, struct file *filp){

    //Étape 0: déterminer le port sur lequel le service open est sollicité (car on a deux ports série)
    int port = MINOR(inode->i_rdev);
    struct perso *p;

    //ressources partagées: protection d'accès avec un spinlock 
    spin_lock(&perso[port].MySpin);

    //Étape 1: vérification de l'usager
    if(perso[port].uid == 0){ //si pas d'usager actif
        perso[port].uid = current_cred()->uid.val; //ok
        filp->private_data = (void *) &perso[port]; //on rattache le port à la bonne structure perso
    }else if((perso[port].uid != 0) && (perso[port].uid == current_cred()->uid.val)){  //si usager actif et que c'est le même, ok
        filp->private_data = (void *) &perso[port]; //ok
    }else{        //un autre utilisateur essaye d'ouvrir le port
        return -EBUSY;
    }

    p = ((struct perso *) filp->private_data);
    
    //Étape 2: vérification du mode
    switch(filp->f_flags & O_ACCMODE){
        case O_RDONLY:
            if(p->read==0){
                p->read = 1;
                //le buffer circulaire est initialisé dans le open pour être sur du mode afin de ne pas consommer de la mémoire pour rien
                if(p->already_open==0){
                    p->circular_buffer->Buffer = (char*)kmalloc(BUFF_SIZE*sizeof(char),GFP_KERNEL);  //DONE (jespère)
                    p->circular_buffer->buff_size = BUFF_SIZE;
                    p->circular_buffer->head = 0;
                    p->circular_buffer->tail = 0;
                    p->circular_buffer->empty = 1;
                    p->circular_buffer->full = 0;
                    p->already_open = 1;
                }
                //placer le port série en mode réception (active l'interruption de réception)(hardware)
            }
            else{
                spin_unlock(&p->MySpin);
                return -ENOTTY;   //ouverture du fichier dans un mode déjà ouvert: lecture
            }
            break;

        case O_WRONLY:
            if(p->write==0){
                p->write = 1;
                if(p->already_open ==0){
                    p->circular_buffer->Buffer = (char*)kmalloc(BUFF_SIZE*sizeof(char),GFP_KERNEL);  //DONE (jespère)
                    p->circular_buffer->buff_size = BUFF_SIZE;
                    p->circular_buffer->head = 0;
                    p->circular_buffer->tail = 0;
                    p->circular_buffer->empty = 1;
                    p->circular_buffer->full = 0;
                    p->already_open = 1;
                }
            }
            else{
                spin_unlock(&p->MySpin);
                return -ENOTTY;   //ouverture du fichier dans un mode déjà ouvert: écriture
            }
            break;

        case O_RDWR:
            if(p->read==0 && p->write==0){
                p->read = 1;
                p->write = 1;
                if(p->already_open ==0){
                    p->circular_buffer->Buffer = (char*)kmalloc(BUFF_SIZE*sizeof(char),GFP_KERNEL);  //DONE (jespère)
                    p->circular_buffer->buff_size = BUFF_SIZE;
                    p->circular_buffer->head = 0;
                    p->circular_buffer->tail = 0;
                    p->circular_buffer->empty = 1;
                    p->circular_buffer->full = 0;
                    p->already_open = 1;
                }
                //placer le port série en mode réception (active l'interruption de réception)

            }
            else{
                spin_unlock(&p->MySpin);
                return -ENOTTY;   //ouverture du fichier dans un mode déjà ouvert: lecture et/ou écriture
            }
            break;

        default:
            //on n'oublie pas de libérer le spinlock
            spin_unlock(&p->MySpin);
            printk(KERN_WARNING"Big Driver : Open error ! Wrong mode !\n");
            return -EINVAL;  //par défaut: mode d'ouverture invalide
            break;
    }

    spin_unlock(&p->MySpin);

    return 0; //ouverture demandée par l'usager dans le mode demandé acceptée
}

static int MyModule_release(struct inode *inode, struct file *filp){
    //La fonction de release sert à:
    //1: libérer les données personnelles (filp->private_data)
    //2: arrêter le matériel à la dernière "libération"

    //int port = MINOR(inode->i_rdev);

    struct perso *p;
    p = ((struct perso *) filp->private_data);

    //ressources partagées: protection d'accès avec un spinlock (car le temps d'exécution de la fonction est court)
    spin_lock(&p->MySpin);

    if(filp->f_flags & O_RDONLY){
      p->read = 0;
      //désactiver l'interruption de réception
    }
    else if(filp->f_flags & O_WRONLY){
      p->write = 0;
    }
    else if(filp->f_flags & O_RDWR){
      p->read = 0;
      p->write = 0;
      //désactiver l'interruption de réception
    }
    else{
        spin_unlock(&p->MySpin);
        return -EINVAL;
    }
    p->uid = 0; //on libère l'usager

    spin_unlock(&p->MySpin);

    filp->private_data = NULL; //on libère les données personnelles

    return 0;
}


static ssize_t MyModule_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){
    //kernel space -> user space
    int n = 0;
    char local_buffer[LOCALBUFF_SIZE];   // Tampon local
    int port = MINOR(filp->f_inode->i_rdev);
    int bytes_to_read = 0;
    int bytes_read = 0;
    int bytes_count = 0;
    int bytes_not_copied = 0;
    int offset_user = 0;
    struct perso *p = (struct perso *)filp->private_data;

    //Étape 1: vérifier que le port est ouvert en mode lecture + protection d'accès 
    spin_lock(&p->MySpin);
    if (!p->read) {
        spin_unlock(&p->MySpin);
        return -EINVAL;
    }

    //Étape 2: vérifier que le tampon circulaire n'est pas vide
    if(p->circular_buffer->empty){
        spin_unlock(&p->MySpin);
        return -EAGAIN;
    }

    bytes_to_read = p->circular_buffer->head-p->circular_buffer->tail;	//nombre de bytes dispo dans le buffer circulaire à lire
    spin_unlock(&p->MySpin);
    
    //Étape 3: aller chercher les données dans le buffer circulaire et les remplir dans le tampon local (au maximum de la taille de celui-ci) puis les copier dans l'espace utilisateur

    //protection d'accès avec sémaphore car le temps d'exécution de la fonction est long
    if(down_interruptible(&p->MySem)){ //si le sémaphore est pris par un autre processus
        return -ERESTARTSYS;  //permet de détecter IRQ systeme
    }
    
    while(bytes_to_read==0){
        up(&p->MySem); //on libère le sémaphore avant de dormir
        if(filp->f_flags & O_NONBLOCK){  //si l'usager a demandé un mode non bloquant
            return -EAGAIN; //on retourne une erreur
        }
        else{
            if(wait_event_interruptible(&p->RdQ, bytes_to_read>0)){  //sinon, dort jusqu'à ce qu'il y ait des données dans le buffer circulaire (mode bloquant)
                return -ERESTARTSYS;  //permet de détecter IRQ systeme
            }
            down(&p->MySem); //on reprend le sémaphore
            bytes_to_read = p->circular_buffer->head-p->circular_buffer->tail;
        }
    }

    bytes_count = (bytes_to_read>bytes_count)?bytes_count:bytes_to_read;  //on prend le minimum entre le nombre de données demandées et le nombre de données disponibles

    while(bytes_to_read){

        bytes_count--;
        local_buffer[n] = p->circular_buffer->tail;  //on copie les données dans le tampon local

        //Étape 4: copier les données dans l'espace utilisateur : dès que le buffer local est rempli ou que le nombre de données voulu est atteint
        if(n==LOCALBUFF_SIZE-1 || bytes_count == 0){
            bytes_not_copied = copy_to_user(buf + offset_user, local_buffer, n+1);  //on copie les données dans l'espace utilisateur
            offset_user = offset_user + LOCALBUFF_SIZE;
            if(bytes_not_copied){  //copy_to_user retourne le nombre de bytes non copiés
                return bytes_not_copied;  //on retourne le nombre de bytes non copiés
            } 
        }
        n = (n+1)%LOCALBUFF_SIZE;
        p->circular_buffer->tail = (p->circular_buffer->tail + 1) % BUFF_SIZE; //on incrémente le pointeur de lecture en restant dans les limites du buffer
        if(p->circular_buffer->tail==p->circular_buffer->head){
            p->circular_buffer->empty = 1;
        }

        p->circular_buffer->full = 0;
        bytes_read++;
    }

    up(&p->MySem); //on libère le sémaphore

    //Étape 5: retourner le nombre de données lues
    return bytes_read;
}


static ssize_t MyModule_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos){
    //user space -> kernel space
    int n = 0;
    char local_buffer[LOCALBUFF_SIZE];   // Tampon local
    int port = MINOR(filp->f_inode->i_rdev);
    int bytes_to_write = 0;
    int bytes_written = 0;
    int bytes_count = 0;
    int bytes_not_copied = 0;
    int offset_user = 0;
    struct perso *p = (struct perso *)filp->private_data;


    //Étape 1: vérifier que le port est ouvert en mode lecture + protection d'accès 
    spin_lock(&p->MySpin);
    if (!p->write) {
        spin_unlock(&p->MySpin);
        return -EINVAL;
    }

    //Étape 2: vérifier que le tampon circulaire n'est pas plein
    if(p->circular_buffer->full){
        spin_unlock(&p->MySpin);
        return -EAGAIN;
    }


    bytes_to_write = p->circular_buffer->head-p->circular_buffer->tail;	//nombre de bytes dispo dans le buffer circulaire à écrire

    spin_unlock(&p->MySpin);

    if(down_interruptible(&p->MySem)){ //si le sémaphore est pris par un autre processus
        return -ERESTARTSYS;  //permet de détecter IRQ systeme
    }

    //si le tampon circulaire est plein
    while(bytes_to_write==0){
        up(&p->MySem); //on libère le sémaphore avant de dormir
        if(filp->f_flags & O_NONBLOCK){  //si l'usager a demandé un mode non bloquant
            return -EAGAIN; //on retourne une erreur
        }
        else{
            if(wait_event_interruptible(&p->WrQ, bytes_to_write>0)){  //sinon, dort jusqu'à ce qu'il y ait de la place dans le buffer circulaire (mode bloquant)
                return -ERESTARTSYS;  //permet de détecter IRQ systeme
            }
            down(&p->MySem); //on reprend le sémaphore
            bytes_to_write = p->circular_buffer->head-p->circular_buffer->tail;
        }
    }  

    bytes_count = (bytes_to_write>bytes_count)?bytes_count:bytes_to_write;	//on prend le minimum entre le nombre de données demandées et le nombre de données disponibles

    //Étape 3: Pilote place les données dans tampon TxBuf une à la fois
    while(bytes_to_write){

        //copier les données depuis l'espace utilisateur dans le tampon local : ce fait lorsque celui ci est vide
        if(n==0){
            if(bytes_count < LOCALBUFF_SIZE){   //si le nombre de données qu'il reste a ecrire est plus petit que le buffer local
                bytes_not_copied = copy_from_user(local_buffer + offset_user, buf, bytes_count); //on copie les données dans l'espace utilisateur
            }else{  //si le nombre de données qu'il reste a ecrire est plus grand que le buffer local
                bytes_not_copied = copy_from_user(local_buffer + offset_user, buf, LOCALBUFF_SIZE); //on copie les données dans l'espace utilisateur
                offset_user = offset_user + LOCALBUFF_SIZE;
            }
            if(bytes_not_copied){  //copy_to_user retourne le nombre de bytes non copiés
                return bytes_not_copied;  //on retourne le nombre de bytes non copiés
             }
        }

        p->circular_buffer->head = local_buffer[n];  //on copie les données dans le tampon circulaire

        n = (n+1)%LOCALBUFF_SIZE;
        p->circular_buffer->head = (p->circular_buffer->head + 1) % BUFF_SIZE; //on incrémente le pointeur d'écriture en restant dans les limites du buffer
        if(p->circular_buffer->head==p->circular_buffer->tail){
            p->circular_buffer->full = 1;
        }

        p->circular_buffer->empty = 0;
        bytes_count--;
        bytes_written++;
        wake_up_interruptible(&p->WrQ); //on réveille les tâches qui dorment dans la queue
    }

    up(&p->MySem); //on libère le sémaphore

    //Étape 5: retourner le nombre de données écrites
    return bytes_written;
}


static int MyModule_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
    //ioctl est une fonction qui permet de communiquer avec le pilote
    int retval;
    
    struct perso *p = (struct perso *)filp->private_data;
    spin_lock(&p->MySpin);
    
    switch(cmd){
        case SetBaudRate:
            //TODO WHEN WE CAN
            //- Permet de changer la vitesse de communication du Port Série. La valeur permise se situe dans la plage 50 à 115200 Baud
            if(arg<BAUDRATE_MIN || arg>BAUDRATE_MAX){
                return -EINVAL;
            }
            spin_unlock(&p->MySpin);
            break;

        case SetDataSize:
            //TODO WHEN WE CAN
            //- Permet de changer la taille des données de communication. La valeur permise se situe dans la plage 5 à 8 bits
            if(arg<DATA_SIZE_MIN || arg>DATA_SIZE_MAX){
                return -EINVAL;
            }
            spin_unlock(&p->MySpin);
            break;

        case SetParity:
            //TODO WHEN WE CAN
            //- Permet de choisir le type de parité qui sera utilisée lors des communications. La valeur permise est :0 : Pas de parité, 1 : Parité impaire, 2 : Parité paire 
            if(arg<PARITY_NONE || arg>PARITY_EVEN){
                return -EINVAL;
            }
            spin_unlock(&p->MySpin);
            break;
        
        case GetBufSize:
            //DONE
            //- Retourne la taille des tampons RxBuf et TxBuf (note : les deux ont la même taille)
            retval = p->circular_buffer->buff_size;
            spin_unlock(&p->MySpin);
            break;

        case SetBufSize:
            //DONE (j'espère)
            /*- Redimensionne les tampons RxBuf et TxBuf, tout en s’assurant
            que les données qui s’y trouvent sont préservées et que les index
            (TxInIdx, TxOutIdx, RxInIdx et RxOutIdx) sont ajustés en
            conséquence.
            - Un code d’erreur est retourné si les anciens RxBuf et TxBuf
            contiennent trop de données pour la nouvelle taille. L’opération
            n’est alors pas effectuée, c’est-à-dire que la taille n’est pas
            changée.
            - Cette commande n’est accessible que pour un usager ayant des
            permissions d’administrateur (CAP_SYS_ADMIN). 
            */

            //1: on check les user perms
            if(!capable(CAP_SYS_ADMIN)){
                return -EPERM;
            }

            //2: on check si old buf data > new buf size
            int old_buff_count = (p->circular_buffer->head - p->circular_buffer->tail + p->circular_buffer->buff_size) % p->circular_buffer->buff_size;
            if(old_buff_count > arg){
                return -EINVAL;
            }
            
            //3: crea° new buffer
            char *new_buffer = (char*)kmalloc(arg*sizeof(char), GFP_KERNEL);
            if(new_buffer==NULL){
                return -ENOMEM;
            }

            //4: copy du old buf dans le new buf
            int i = 0;
            while(p->circular_buffer->tail != p->circular_buffer->head){
                new_buffer[i] = p->circular_buffer->Buffer[p->circular_buffer->tail];
                p->circular_buffer->tail = (p->circular_buffer->tail + 1) % p->circular_buffer->buff_size;
                i++;
            }

            //5: on free old buffer
            kfree(p->circular_buffer->Buffer);

            //6: on oyblie pas l'update buffer et l'update du head/tail
            p->circular_buffer->Buffer = new_buffer;
            p->circular_buffer->buff_size = arg;
            p->circular_buffer->head = i;
            p->circular_buffer->tail = 0;

            spin_unlock(&p->MySpin);
            break;

        default:
            spin_unlock(&p->MySpin);
            return -ENOTTY;

        
    }

    spin_unlock(&p->MySpin);
    return retval;
}
// make -C /usr/src/linux-headers-4.15.18 M=`pwd` modules
// sudo insmod ./MyModule.ko
// sudo rmmod MyModule
// lsmod | grep MyModule

