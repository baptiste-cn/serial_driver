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

#define BUFF_SIZE_DEFAULT 64
#define LOCALBUFF_SIZE 4
#define NB_PORT 2

#define IOCTL_MAGIC_NUMBER 'k'
#define SET_BAUD_RATE _IOW(IOCTL_MAGIC_NUMBER, 0, int)
#define SET_DATA_SIZE _IOW(IOCTL_MAGIC_NUMBER, 1, int)
#define SET_PARITY    _IOW(IOCTL_MAGIC_NUMBER, 2, int)
#define GET_BUF_SIZE  _IOR(IOCTL_MAGIC_NUMBER, 3, int)
#define SET_BUF_SIZE  _IOW(IOCTL_MAGIC_NUMBER, 4, int)

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
static long MyModule_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);


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
	.unlocked_ioctl = 	MyModule_ioctl
};

struct perso{
    int read; 
    int write;
    int uid;
    int user;
    int head;
    int tail;
    char* circular_buffer;
    uint8_t circular_buffer_size;
    int bytes_to_read;
    int bytes_to_write;
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


/*static int __init mod_init (void) {
    int n;
    alloc_chrdev_region (&My_dev,  0,  NB_PORT, "Serial driver");  //allocation dynamique du major et du minor (numéro d'unité-matériel)

    //les noeuds dus unités-matériels sont créés dans au chargement du module et détruits au déchargement du module
    MyClass = class_create(THIS_MODULE, "MyModule");  //création de la classe
    //MyClass = class_create(THIS_MODULE, "Driver");  //création de la classe
    
    //initialisation de la structure cdev


    for (n = 0; n < NB_PORT; n++) {
        device_create(MyClass, NULL, (My_dev + n), NULL, "MyModuleNode%d", n);
    }

    cdev_init(&My_cdev, &MyModule_fops); //initialisation de la structure cdev pour le pilote


    for (n = 0; n < NB_PORT; n++){
        //création des noeuds dans /dev
        //initialisation des variables de la structure perso
        perso[n].user=0;
        perso[n].read=0;
        perso[n].write=0;
        perso[n].head=0;
        perso[n].tail=0;
        perso[n].circular_buffer_size = BUFF_SIZE_DEFAULT;
        perso[n].bytes_to_read = 0;
        perso[n].bytes_to_write = BUFF_SIZE_DEFAULT;
        perso[n].already_open=0;
        sema_init(&perso[n].MySem, 1);
        spin_lock_init(&perso[n].MySpin);
        init_waitqueue_head(&perso[n].RdQ);
        init_waitqueue_head(&perso[n].WrQ);
    }


    printk(KERN_WARNING"Big Driver : Hello World ! Serial driver = %u\n", MyModule_X);

    printk(KERN_WARNING"avant : debut open\n");
    printk("avant : debut open test\n");

    //A FAIRE EN TOUT DERNIER 
    cdev_add(&My_cdev, My_dev, NB_PORT); 

    

    return 0;
}*/


static int __init mod_init (void) {
    int n;
    if(alloc_chrdev_region (&My_dev,  0,  NB_PORT, "Serial driver") < 0){ //allocation dynamique du major et du minor (numÃ©ro d'unitÃ©-matÃ©riel)
        printk(KERN_WARNING"Big Driver : Error allocating major and minor !\n");
        return -EBUSY;
    }

    MyClass = class_create(THIS_MODULE, "Driver");  //crÃ©ation de la classe
    if(IS_ERR(MyClass)){
        printk(KERN_WARNING"Big Driver : Error creating class !\n");
        unregister_chrdev_region(My_dev, NB_PORT);
        return -EBUSY;
    }

    for (n = 0; n < NB_PORT; n++) {
        device_create(MyClass, NULL, (My_dev + n), NULL, "MyModuleNode%d", n);
    }

    cdev_init(&My_cdev, &MyModule_fops); //initialisation de la structure cdev pour le pilote


    for (n = 0; n < NB_PORT; n++){
        //création des noeuds dans /dev
        //initialisation des variables de la structure perso
        perso[n].user=0;
        perso[n].read=0;
        perso[n].write=0;
        perso[n].head=0;
        perso[n].tail=0;
        perso[n].circular_buffer_size = BUFF_SIZE_DEFAULT;
        perso[n].bytes_to_read = 0;
        perso[n].bytes_to_write = BUFF_SIZE_DEFAULT;
        perso[n].already_open=0;
        sema_init(&perso[n].MySem, 1);
        spin_lock_init(&perso[n].MySpin);
        init_waitqueue_head(&perso[n].RdQ);
        init_waitqueue_head(&perso[n].WrQ);
    }

    //CDEV ADD: A FAIRE EN TOUT DERNIER 
    if(cdev_add(&My_cdev, My_dev, NB_PORT) < 0){
        printk(KERN_WARNING"Big Driver : Error adding cdev !\n");
        for (n = 0; n < NB_PORT; n++) {
            device_destroy(MyClass, (My_dev + n));  //destruction des noeuds
        }
        class_destroy(MyClass);
        unregister_chrdev_region(My_dev, NB_PORT);
        return -EBUSY;
    }

    printk(KERN_WARNING"Big Driver : Hello World ! Serial driver = %u\n", MyModule_X);

    return 0;
}


static void __exit mod_exit (void) {
    int n;
    cdev_del(&My_cdev); //suppression de la structure cdev

    for (n = 0; n < NB_PORT; n++) {
    	device_destroy(MyClass, (My_dev + n));  //destruction des noeuds
        if(perso[n].circular_buffer!=NULL){
            kfree(perso[n].circular_buffer);
        }
    }
    class_destroy(MyClass);

    unregister_chrdev_region(My_dev, NB_PORT);

    //ici on pourrait faire un kfree(Pilote) si on avait fait un kmalloc(Pilote) dans le init

    printk(KERN_WARNING"Big Driver : Goodbye cruel World !\n");

}   

static int MyModule_open(struct inode *inode, struct file *filp){
    
    struct perso *p;
    int port = MINOR(inode->i_rdev);
    printk(KERN_ALERT"debut open");
    
    
    //Étape 0: déterminer le port sur lequel le service open est sollicité (car on a deux ports série)
    
    printk(KERN_INFO"minor:%d\n", port);
    

    //ressources partagées: protection d'accès avec un spinlock 
    spin_lock(&(perso[port].MySpin));

    //Étape 1: vérification de l'usager
    if(perso[port].uid == 0){ //si pas d'usager actif
        perso[port].uid = current_cred()->uid.val; //ok
        filp->private_data = (void *) &perso[port]; //on rattache le port à la bonne structure perso
    }else if((perso[port].uid != 0) && (perso[port].uid == current_cred()->uid.val)){  //si usager actif et que c'est le même, ok
        filp->private_data = (void *) &perso[port]; //ok
    }else{        //un autre utilisateur essaye d'ouvrir le port
        //return -EBUSY;
        spin_unlock(&perso[port].MySpin);
        return -10;
    }

    p = ((struct perso *) filp->private_data);

    //spin_lock(&p->MySpin);
    
    //Étape 2: vérification du mode
    switch(filp->f_flags & O_ACCMODE){
        //printk(KERN_INFO"LA C OK\n");
        case O_RDONLY:
            printk(KERN_INFO"LA C OK\n");
            if(p->read==0){
                printk(KERN_INFO"READ AVANT: %d\n", p->read);
                printk(KERN_INFO"LA C OK1\n");
                p->read = 1;
                printk(KERN_INFO"READ APRES: %d\n", p->read);
                printk(KERN_INFO"LA C OK2\n");
                //le buffer circulaire est initialisé dans le open pour être sur du mode afin de ne pas consommer de la mémoire pour rien
                if(p->already_open==0){
                    p->circular_buffer = (char*)kmalloc(BUFF_SIZE_DEFAULT*sizeof(char),GFP_KERNEL);
                    p->already_open = 1;
                }
                //placer le port série en mode réception (active l'interruption de réception)(hardware)
            }
            else{
                spin_unlock(&p->MySpin);
                printk(KERN_WARNING"Big Driver : Open error ! Wrong mode !\n");
                return -ENOTTY;   //ouverture du fichier dans un mode déjà ouvert: lecture
            }
            break;

        case O_WRONLY:
            if(p->write==0){
                p->write = 1;
                if(p->already_open ==0){
                    p->circular_buffer = (char*)kmalloc(BUFF_SIZE_DEFAULT*sizeof(char),GFP_KERNEL);
                    p->already_open = 1;
                }
            }
            else{
                spin_unlock(&p->MySpin);
                printk(KERN_WARNING"Big Driver : Open error ! Wrong mode !\n");
                return -ENOTTY;   //ouverture du fichier dans un mode déjà ouvert: écriture
            }
            break;

        case O_RDWR:
            if(p->read==0 && p->write==0){
                p->read = 1;
                p->write = 1;
                if(p->already_open ==0){
                    p->circular_buffer = (char*)kmalloc(BUFF_SIZE_DEFAULT*sizeof(char),GFP_KERNEL);
                    p->already_open = 1;
                }
                //placer le port série en mode réception (active l'interruption de réception)

            }
            else{
                spin_unlock(&p->MySpin);
                printk(KERN_WARNING"Big Driver : Open error ! Wrong mode !\n");
                return -ENOTTY;   //ouverture du fichier dans un mode déjà ouvert: lecture et/ou écriture
            }
            break;

        default:
            //on n'oublie pas de libérer le spinlock
            spin_unlock(&p->MySpin);
            printk(KERN_WARNING"Big Driver : Open error ! Wrong mode !\n");
            return -30;  //par défaut: mode d'ouverture invalide
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


    struct perso *p = ((struct perso *) filp->private_data);

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

    if((p->read == 0) && (p->write == 0)){
        p->uid = 0;
    }
    //p->uid = 0; //on libère l'usager

    spin_unlock(&p->MySpin);

    filp->private_data = NULL; //on libère les données personnelles

    printk(KERN_WARNING "RELEASE\n");

    return 0;
}


static ssize_t MyModule_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){
    //kernel space -> user space
    int n = 0;
    char local_buffer[LOCALBUFF_SIZE];   // Tampon local
    //int bytes_to_read = 0;
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
   /* if(p->circular_buffer_empty){
        spin_unlock(&p->MySpin);
        return -EAGAIN;
    }*/

    p->bytes_to_read = p->head-p->tail;    //si égal 0 alors forcement vide
    spin_unlock(&p->MySpin);
    
    //Étape 3: aller chercher les données dans le buffer circulaire et les remplir dans le tampon local (au maximum de la taille de celui-ci) puis les copier dans l'espace utilisateur

    //protection d'accès avec sémaphore car le temps d'exécution de la fonction est long
    if(down_interruptible(&p->MySem)){ //si le sémaphore est pris par un autre processus
        return -ERESTARTSYS;  //permet de détecter IRQ systeme
    }
    
    while(p->bytes_to_read==0){
        up(&p->MySem); //on libère le sémaphore avant de dormir
        if(filp->f_flags & O_NONBLOCK){  //si l'usager a demandé un mode non bloquant
        printk(KERN_WARNING "pas de donnée dispo et non bloquant\n");
            return -EAGAIN; //on retourne une erreur
        }
        else{
            if(wait_event_interruptible(p->RdQ, p->bytes_to_read>0)){  //sinon, dort jusqu'à ce qu'il y ait des données dans le buffer circulaire (mode bloquant)
                return -ERESTARTSYS;  //permet de détecter IRQ systeme
            }
            //down(&p->MySem); //on reprend le sémaphore
            //bytes_to_read = p->head-p->tail;
            if(down_interruptible(&p->MySem)){ //si le sémaphore est pris par un autre processus
            return -ERESTARTSYS;  //permet de détecter IRQ systeme
            }
        }

    }

    p->bytes_to_read = p->head - p->tail;
    bytes_count = (p->bytes_to_read>count)?count:p->bytes_to_read;  //on prend le minimum entre le nombre de données demandées et le nombre de données disponibles

    printk(KERN_WARNING "MyMod: count value is %u", (int) bytes_count);

    while(bytes_count){

        bytes_count--;
        local_buffer[n] = p->circular_buffer[p->tail];  //on copie les données dans le tampon local

        //Étape 4: copier les données dans l'espace utilisateur : dès que le buffer local est rempli ou que le nombre de données voulu est atteint
        if(n==LOCALBUFF_SIZE-1 || bytes_count == 0){
            bytes_not_copied = copy_to_user(buf + offset_user, &local_buffer, n+1);  //on copie les données dans l'espace utilisateur
            offset_user = offset_user + LOCALBUFF_SIZE;
            if(bytes_not_copied){  //copy_to_user retourne le nombre de bytes non copiés
                return bytes_not_copied;  //on retourne le nombre de bytes non copiés
            } 
        }
        n = (n+1)%LOCALBUFF_SIZE;
        p->tail = (p->tail + 1) % (p->circular_buffer_size); //on incrémente le pointeur de lecture en restant dans les limites du buffer
        /*if(p->tail==p->head){
            p->circular_buffer_empty = 1;
        }*/

        //p->circular_buffer_full = 0;
        bytes_read++;
    }

    p->bytes_to_write = p->circular_buffer_size - (p->head-p->tail);
    up(&p->MySem); //on libère le sémaphore
    wake_up_interruptible(&p->WrQ);

    //Étape 5: retourner le nombre de données lues
    return bytes_read;
}


static ssize_t MyModule_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos){
    //user space -> kernel space
    int n = 0;
    char local_buffer[LOCALBUFF_SIZE];   // Tampon local
    //int bytes_to_write = 0;
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
    /*if(p->circular_buffer_full){
        spin_unlock(&p->MySpin);
        return -EAGAIN;
    }*/


   // bytes_to_write = p->tail-p->head;	//nombre de bytes dispo dans le buffer circulaire à écrire
	


    spin_unlock(&p->MySpin);

    if(down_interruptible(&p->MySem)){ //si le sémaphore est pris par un autre processus
        return -ERESTARTSYS;  //permet de détecter IRQ systeme
    }

    p->bytes_to_write = p->circular_buffer_size - (p->head-p->tail);

    //si le tampon circulaire est plein
    while(p->bytes_to_write == 0){
        up(&p->MySem); //on libère le sémaphore avant de dormir
        if(filp->f_flags & O_NONBLOCK){  //si l'usager a demandé un mode non bloquant
           printk("write o non block");
            return -EAGAIN; //on retourne une erreur
        }
        else{
            if(wait_event_interruptible(p->WrQ, p->bytes_to_write>0)){  //sinon, dort jusqu'à ce qu'il y ait de la place dans le buffer circulaire (mode bloquant)
                return -ERESTARTSYS;  //permet de détecter IRQ systeme
            }
            //down(&p->MySem); //on reprend le sémaphore
           // bytes_to_write = p->tail-p->head;
            if(down_interruptible(&p->MySem)){ //si le sémaphore est pris par un autre processus
                return -ERESTARTSYS;  //permet de détecter IRQ systeme
            }
        }
    }

    p->bytes_to_write = p->circular_buffer_size - (p->head-p->tail);
    //bytes_count = (bytes_to_write>count)?count:bytes_to_write;	//on prend le minimum entre le nombre de données demandées et le nombre de données disponibles


	bytes_count = (p->bytes_to_write>count)?count:p->bytes_to_write;
	
	printk("count write : %d\n", bytes_count);


    //Étape 3: Pilote place les données dans tampon TxBuf une à la fois
    while(bytes_count){

        //copier les données depuis l'espace utilisateur dans le tampon local : ce fait lorsque celui ci est vide
        if(n==0){
            if(bytes_count < LOCALBUFF_SIZE){   //si le nombre de données qu'il reste a ecrire est plus petit que le buffer local
                bytes_not_copied = copy_from_user(&local_buffer, buf+ offset_user, bytes_count); //on copie les données dans l'espace utilisateur
            }else{  //si le nombre de données qu'il reste a ecrire est plus grand que le buffer local
                bytes_not_copied = copy_from_user(&local_buffer, buf+ offset_user, LOCALBUFF_SIZE); //on copie les données dans l'espace utilisateur
                offset_user = offset_user + LOCALBUFF_SIZE;
            }
            if(bytes_not_copied){  //copy_to_user retourne le nombre de bytes non copiés
                return bytes_not_copied;  //on retourne le nombre de bytes non copiés
             }
        }

        p->circular_buffer[p->head] = local_buffer[n];  //on copie les données dans le tampon circulaire

        n = (n+1)%LOCALBUFF_SIZE;
        p->head = (p->head + 1) % (p->circular_buffer_size); //on incrémente le pointeur d'écriture en restant dans les limites du buffer
        /*if(p->head==p->tail){
            p->circular_buffer_full = 1;
        }*/

       // p->circular_buffer_empty = 0;
        bytes_count--;
        bytes_written++;
       }

    p->bytes_to_read = p->head - p->tail;
    up(&p->MySem); //on libère le sémaphore
    wake_up_interruptible(&p->RdQ); //on réveille les tâches qui dorment dans la queue

//Étape 5: retourner le nombre de données écrites
    return bytes_written;
}


static long MyModule_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
    //ioctl est une fonction qui permet de communiquer avec le pilote
    int retval = 0;
    char *new_buffer;
    int old_buff_count;
    int i = 0;
    
    struct perso *p = (struct perso *)filp->private_data;
    spin_lock(&p->MySpin);
    
    switch(cmd){
        case SET_BAUD_RATE:
            //TODO WHEN WE CAN
            //- Permet de changer la vitesse de communication du Port Série. La valeur permise se situe dans la plage 50 à 115200 Baud
            if(arg<BAUDRATE_MIN || arg>BAUDRATE_MAX){
                spin_unlock(&p->MySpin);
                return -EINVAL;
            }
            break;

        case SET_DATA_SIZE:
            //TODO WHEN WE CAN
            //- Permet de changer la taille des données de communication. La valeur permise se situe dans la plage 5 à 8 bits
            if(arg<DATA_SIZE_MIN || arg>DATA_SIZE_MAX){
                spin_unlock(&p->MySpin);
                return -EINVAL;
            }
            break;

        case SET_PARITY:
            //TODO WHEN WE CAN
            //- Permet de choisir le type de parité qui sera utilisée lors des communications. La valeur permise est :0 : Pas de parité, 1 : Parité impaire, 2 : Parité paire 
            if(arg<PARITY_NONE || arg>PARITY_EVEN){
                spin_unlock(&p->MySpin);
                return -EINVAL;
            }
            break;
        
        case GET_BUF_SIZE:
            //DONE
            //- Retourne la taille des tampons RxBuf et TxBuf (note : les deux ont la même taille)
            retval = p->circular_buffer_size;
            break;

        case SET_BUF_SIZE:
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
                spin_unlock(&p->MySpin);
                return -EPERM;
            }

            //2: on check si old buf data > new buf siz
            old_buff_count = (p->head - p->tail + p->circular_buffer_size) % p->circular_buffer_size;
            if(old_buff_count > arg){
                spin_unlock(&p->MySpin);
                return -EINVAL;
            }
            
            //3: crea° new buffer
            new_buffer = (char*)kmalloc(arg*sizeof(char), GFP_KERNEL);
            if(new_buffer==NULL){
                spin_unlock(&p->MySpin);
                return -ENOMEM;
            }

            //4: copy du old buf dans le new buf
            i = 0;
            while(p->tail != p->head){
                new_buffer[i] = p->circular_buffer[p->tail];
                p->tail = (p->tail + 1) % p->circular_buffer_size;
                i++;
            }
            /*
            for(i=0; i<old_buff_count; i++){
                memcpy(&new_buffer[i], &p->circular_buffer[(p->tail + i) % p->circular_buffer_size], 1);
            }
            */

            //5: on free old buffer
            kfree(p->circular_buffer);

            //6: on oyblie pas l'update buffer et l'update du head/tail
            p->circular_buffer = new_buffer;
            p->circular_buffer_size = arg;
            p->head = i;
            p->tail = 0;

            break;

        default:
            spin_unlock(&p->MySpin);
            return -ENOTTY;

        
    }

    spin_unlock(&p->MySpin);
    return retval;
}

module_init(mod_init);
module_exit(mod_exit);




// make -C /usr/src/linux-headers-4.15.18 M=`pwd` modules
// sudo insmod ./MyModule.ko
// sudo rmmod MyModule
// lsmod | grep MyModule

