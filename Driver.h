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
#define PORT_SIZE 8

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

//freq 1.8432MHz
#define FREQUENCY 1843200
#define DL ((FREQUENCY / BAUDRATE_MAX)) 

#define RX_REGISTER  (perso[port].address + 0x00)   //Receiver Buffer Register (read only)
#define TX_REGISTER  (perso[port].address + 0x00)   //Transmitter Holding Register (write only)
#define DLL_REGISTER (perso[port].address + 0x00)   //Divisor Latch LSB (read/write)
#define DLM_REGISTER (perso[port].address + 0x01)   //Divisor Latch MSB (read/write)
#define IER_REGISTER (perso[port].address + 0x01)   //Interrupt Enable Register (read/write)
#define IIR_REGISTER (perso[port].address + 0x02)   //Interrupt Identification Register (read only)
#define FCR_REGISTER (perso[port].address + 0x02)   //FIFO Control Register (write only)
#define LCR_REGISTER (perso[port].address + 0x03)   //Line Control Register (read/write)
#define MCR_REGISTER (perso[port].address + 0x04)   //Modem Control Register (read/write)
#define LSR_REGISTER (perso[port].address + 0x05)   //Line Status Register (read only)
#define MSR_REGISTER (perso[port].address + 0x06)   //MODEM Status Register (read only)
#define SCR_REGISTER (perso[port].address + 0x07)   //Scratch Register (read/write)

//déclarations de variables
uint8_t DLAB, DLL, DLM, IER, IIR, FCR, LCR, MCR, LSR, MSR, SCR;
uint16_t LCR;

//déclarations de fonctions
static int MyModule_open(struct inode *inode, struct file *filp);
static int MyModule_release(struct inode *inode, struct file *filp);
static ssize_t MyModule_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
static ssize_t MyModule_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
static long MyModule_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

int MyModule_X = 5;


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
    uint32_t address;
    uint8_t PortIRQ;
};
struct perso perso[NB_PORT];