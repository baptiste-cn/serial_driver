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


#define BUFF_SIZE 64
#define LOCALBUFF_SIZE 4
#define NB_PORT 2


MODULE_AUTHOR("Bruno");
MODULE_LICENSE("Dual BSD/GPL");

int MyModule_X = 5;

//déclarations de fonctions
static int MyModule_open(struct inode *inode, struct file *filp);
static int MyModule_release(struct inode *inode, struct file *filp);
static ssize_t MyModule_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
static ssize_t MyModule_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);


module_param(MyModule_X, int, S_IRUGO);

EXPORT_SYMBOL_GPL(MyModule_X);  //permet de rendre la variable locale Driver_X visible par les autres modules

dev_t My_dev;
struct class *MyClass;

struct file_operations MyModule_fops = {
	.owner		=	THIS_MODULE,
	.read		=	MyModule_read,
	.write		=	MyModule_write,
	.open		=	MyModule_open,
	.release	=	MyModule_release
};

struct cdev My_cdev;

struct perso{
    int read; 
    int write;
    int uid;
    int head;
    int tail;
    char* circular_buffer;
    int bytes_count;
    int bytes_written;
    int bytes_read;
};
struct perso perso[NB_PORT];

static int user;  

//déclaration du sémaphore
static struct semaphore MySem;

//déclaration de la queue MyQ
static DECLARE_WAIT_QUEUE_HEAD(MyQ);

int N;
int n;

static int __init mod_init (void) {
    int n;
    alloc_chrdev_region (&My_dev,  0,  NB_PORT, "MonPremierPilote");

    MyClass = class_create(THIS_MODULE, "MyModule");
    for (n = 0; n < NB_PORT; n++) {
    	device_create(MyClass, NULL, (My_dev + n), NULL, "MyModuleNode%d", n);
    }

    user=0;

    cdev_init(&My_cdev, &MyModule_fops);
    cdev_add(&My_cdev, My_dev, NB_PORT); 

    printk(KERN_WARNING"module : Hello World ! MyModule_X = %u\n", MyModule_X);

    return 0;
}

static void __exit mod_exit (void) {
    int n;
    cdev_del(&My_cdev);

    for (n = 0; n < NB_PORT; n++) {
    	device_destroy(MyClass, (My_dev + n));
    }
    class_destroy(MyClass);

    unregister_chrdev_region(My_dev, NB_PORT);

    printk(KERN_WARNING"module : Goodbye cruel World !\n");
}   

module_init(mod_init);
module_exit(mod_exit);

static int MyModule_open(struct inode *inode, struct file *filp){

    //Étape 0: déterminer le port sur lequel le service open est sollicité (car on a deux ports série)
    int port = MINOR(inode->i_rdev);
    struct perso *p;

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
                //placer le port série en mode réception (active l'interruption de réception)(hardware)
                //allocation du buffer circulaire
                // char* circular_buffer = kmalloc(BUFF_SIZE*sizeof(char), GFP_KERNEL);
                // p->head = (void *) &circular_buffer;
            }
            else{
                return -ENOTTY;   //ouverture du fichier dans un mode déjà ouvert: lecture
            }
            break;

        case O_WRONLY:
            if(p->write==0){
                p->write = 1;
            }
            else{
                return -ENOTTY;   //ouverture du fichier dans un mode déjà ouvert: écriture
            }
            break;

        case O_RDWR:
            if(p->read==0 && p->write==0){
                p->read = 1;
                p->write = 1;
                //placer le port série en mode réception (active l'interruption de réception)
                //allocation du buffer circulaire
                // char* circular_buffer = kmalloc(BUFF_SIZE*sizeof(char), GFP_KERNEL);
                // p->head = (void *) &circular_buffer;

            }
            else{
                return -ENOTTY;   //ouverture du fichier dans un mode déjà ouvert: lecture et/ou écriture
            }
            break;

        default:
            return -EINVAL;  //par défaut: mode d'ouverture invalide
            break;
    }

    return 0; //ouverture demandée par l'usager dans le mode demandé acceptée
}

static int MyModule_release(struct inode *inode, struct file *filp){
    //La fonction de release sert à:
    //1: libérer les données personnelles (filp->private_data)
    //2: arrêter le matériel à la dernière "libération"

    struct perso *p;
    p = ((struct perso *) filp->private_data);

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
      return -EINVAL;
    }
    p->uid = 0; //on libère l'usager

    filp->private_data = NULL; //on libère les données personnelles
    return 0;
}

static ssize_t MyModule_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){
    //Dans la fonction read(), il faut regarder cb de données on a et comparer avec ce que l'usager demande
    //il faut ensuite transférer les données dès qu'on les a (si l'usager en demande 100 et qu'on en a 3 on envoie les 3) pour ne pas bloquer l'usager

    //Étape 0: déterminer le port sur lequel le service open est sollicité (car on a deux ports série)
    //int port = MINOR(filp->f_inode->i_rdev);

    char local_buffer[LOCALBUFF_SIZE];   // Tampon local

    //Étape 1: vérifier que le port est ouvert en mode lecture
    struct perso *p = (struct perso *)filp->private_data;
    if (!p->read) {
        return -EINVAL;
    }

    //Étape 2: vérifier que le tampon circulaire n'est pas vide
    if(p->head==p->tail){
      return -EAGAIN;
    }

    N = p->head-p->tail;
    p->bytes_count = (N>count)?count:N;  //on prend le minimum entre le nombre de données demandées et le nombre de données disponibles

    //local_buffer = kmalloc(bytes_count, GFP_KERNEL);
    
    //Étape 3: aller chercher les données dans le buffer circulaire et les remplir dans le tampon local (au maximum de la taille de celui-ci) puis les copier dans l'espace utilisateur

    down(&MySem); //on capture le sémaphore pour éviter que le buffer circulaire soit modifié pendant qu'on le lit (création d'une région critique)
    
    while(N==0){
        up(&MySem); //on libère le sémaphore avant de dormir
        if(filp->f_flags & O_NONBLOCK){  //si l'usager a demandé un mode non bloquant
            return -EAGAIN; //on retourne une erreur
        }
        else{
            wait_event_interruptible(MyQ, N>0);  //sinon, dort jusqu'à ce qu'il y ait des données dans le buffer circulaire (mode bloquant)
            down(&MySem);
        }
    }

    while(p->bytes_count){
        local_buffer[n] = p->circular_buffer[p->tail];  //on copie les données dans le tampon local

        //Étape 4: copier les données dans l'espace utilisateur
        if(n==4){
            if(copy_to_user(buf, local_buffer, LOCALBUFF_SIZE)){  //copy_to_user retourne le nombre de bytes non copiés
                return -EFAULT;  //et on retourne une erreur
            } 
        }
        n = (n+1)%LOCALBUFF_SIZE;
        p->tail = (p->tail + 1) % BUFF_SIZE; //on incrémente le pointeur de lecture en restant dans les limites du buffer
        p->bytes_count--;
        p->bytes_read++;
        

    }

    up(&MySem); //on libère le sémaphore
    //Étape 5: retourner le nombre de données lues
    return p->bytes_read;

    return 0;
}


static ssize_t MyModule_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos){
    int N;
	int n;
	char local_buffer[LOCALBUFF_SIZE]; // Tampon local
    
    struct perso *p = (struct perso *)filp->private_data;

    if (!p->write) {
        return -EINVAL;
    }


    N = p->tail - p->head;	//nombre de valeur dispo dans le buffer circulaire

    if(N==0){
        return -ENOMEM;
    }

	/*while(N == 0){
		if(filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		wait_event(MyQ, N>0);

	}*/
	
	p->bytes_count = (N>count)?count:N;	//si on veut ecrire plus que ce qui y a de dispo dans le buffer circulaire ecrit N

    if (copy_from_user(local_buffer, buf, p->bytes_count)) {  // copie les données depuis l'espace utilisateur dans le petit buffer : doit etre fait par block?
        return -EFAULT; // Erreur de copie depuis l'espace utilisateur
    }


    down(&MySem);

    //Pilote place les données dans tampon TxBuf une à la fois
    for(n=0; n<p->bytes_count; n++){

        // Copier les données dans le buffer circulaire : a proteger
        p->circular_buffer[p->head] = local_buffer[n]; 

		p->head = (p->head + 1) % BUFF_SIZE;

        wake_up(&MyQ);
	}

    up(&MySem);

    return p->bytes_count;

    return 0;
}

// make -C /usr/src/linux-headers-4.15.18 M=`pwd` modules
// sudo insmod ./MyModule.ko
// sudo rmmod MyModule
// lsmod | grep MyModule

