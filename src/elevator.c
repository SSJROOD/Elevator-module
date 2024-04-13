#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cop4610");
MODULE_DESCRIPTION("kernel module for pt3/elevator");

#define ENTRY_NAME "elevator"
#define MAX_PASSENGERS 5
#define MAX_WEIGHT 70
#define MAX_FLOOR 5 // Added for floor indexing
#define MIN_FLOOR 1 // Added for floor indexing
#define PROC_BUF_SIZE 10000

// Global variables
int wait;   // waiting passengers
int helped; // helped passengers

enum PassengerType
{
    PART_TIME,
    LAWYER,
    BOSS,
    VISITOR,
};
static const int passenger_weights[4] = {10, 15, 20, 5}; // Corrected initialization

enum ElevatorState
{
    OFFLINE,
    IDLE,
    LOADING,
    UP,
    DOWN
};

struct Passenger
{
    int id;
    enum PassengerType type;
    int weight;
    int start_floor;
    int destination_floor;
    struct list_head struct_lister;
};

struct Floor
{
    int floor_number;
    struct list_head passengers; // people waiting on floor
    struct mutex lock;           // mutex for floor
};

struct Elevator
{
    struct list_head passengers; // people on the elevator
    int passenger_count;
    int current_floor;
    int current_weight;
    int running;
    int stopped;
    enum ElevatorState status; // the status of it
    int target_floor;          // the destination floor
    struct mutex lock;         // mutex for elevator
};

static struct Floor floors[5];
static struct task_struct *elevator_thread; // handles this thread

extern int (*STUB_issue_request)(int, int, int);
extern int (*STUB_start_elevator)(void);
extern int (*STUB_stop_elevator)(void);

// Prototypes
static int start_elevator(void);
static int issue_request(int start_floor, int destination_floor, int type);
static int stop_elevator(void);
static int movement(int cfloor, int dfloor);
static int process_passenger(struct Passenger *passenger);
static int loading(void);   // Added prototype
static int unloading(void); // Added prototype

// The unload/loading functions
static void load_passenger(struct Passenger *passenger, struct Elevator *elevator); // Added elevator parameter
static void unload_passenger(struct Passenger *passenger);

// Important structs
static struct Elevator elevator;
static struct Passenger passenger;

static struct proc_dir_entry *elevator_entry; // start

static const struct proc_ops elevator_fops;

int process_passenger(struct Passenger *passenger)
{
    int retval = 0;

    switch (passenger->type)
    {
    case PART_TIME:
        printk(KERN_INFO "Processing a PART_TIME passenger\n");
        return PART_TIME;

    case LAWYER:
        printk(KERN_INFO "Processing a LAWYER passenger\n");
        return LAWYER;

    case BOSS:
        printk(KERN_INFO "Processing a BOSS passenger\n");
        return BOSS;

    case VISITOR:
        printk(KERN_INFO "Processing a VISITOR passenger\n");
        return VISITOR;

    default:
        retval = -EINVAL;
        return retval;
    }
}

int start_elevator(void)
{
    printk(KERN_INFO "Elevator start debug.\n");
    mutex_lock(&elevator.lock);

    if (elevator.running == 1)
    {
        mutex_unlock(&elevator.lock);
        return 1;
    }
    else
    {
        elevator.running = 1;
        elevator.stopped = 0;
        elevator.status = IDLE;
    }

    mutex_unlock(&elevator.lock);
    printk(KERN_INFO "Elevator start good debug.\n");
    return 0;
}

int issue_request(int start_floor, int destination_floor, int type)
{
    printk(KERN_INFO "issue_request called with start_floor=%d, destination_floor=%d, type=%d\n", start_floor,
           destination_floor, type);

    if (type < PART_TIME || type > VISITOR || start_floor < 1 || start_floor > 5 || destination_floor < 1 ||
        destination_floor > 5)
    {
        printk(KERN_WARNING "issue_request: Invalid parameters\n");
        return -EINVAL;
    }

    int weight = passenger_weights[type]; // Correctly use the lookup array
    printk(KERN_INFO "issue_request: weight=%d for type=%d\n", weight, type);

    mutex_lock(&floors[start_floor - 1].lock); // Correct floor indexing
    printk(KERN_INFO "issue_request: Locked floor %d\n", start_floor);

    struct Passenger *new_passenger = kmalloc(sizeof(struct Passenger), GFP_KERNEL);
    if (!new_passenger)
    {
        printk(KERN_WARNING "issue_request: Memory allocation failed for new_passenger\n");
        mutex_unlock(&floors[start_floor - 1].lock);
        return -ENOMEM;
    }

    new_passenger->type = type;
    new_passenger->weight = weight; // Set weight correctly
    new_passenger->start_floor = start_floor;
    new_passenger->destination_floor = destination_floor;
    INIT_LIST_HEAD(&new_passenger->struct_lister);
    list_add_tail(&new_passenger->struct_lister, &floors[start_floor - 1].passengers); // Add to the correct floor
    printk(KERN_INFO "issue_request: New passenger added to floor %d, moving to floor %d\n", start_floor,
           destination_floor);

    mutex_unlock(&floors[start_floor - 1].lock);
    printk(KERN_INFO "issue_request: Unlocked floor %d\n", start_floor);

    return 0;
}

bool check_for_waiting_passengers(void)
{
    int i;
    // Assuming floors array starts from index 0 representing floor 1, up to index 4 representing floor 5
    for (i = 0; i < MAX_FLOOR; i++)
    {
        if (!list_empty(&floors[i].passengers))
        { // Check if there are passengers waiting on floor i
            return true;
        }
    }
    return false; // No passengers waiting on any floor
}

static int movement(int cfloor, int dfloor)
{
    printk(KERN_INFO "movement: Moving from floor %d to floor %d\n", cfloor, dfloor);
    int new_floor;
    if (cfloor == dfloor)
    {
        elevator.status = IDLE;
        printk(KERN_INFO "movement: Elevator is IDLE at floor %d\n", dfloor);
        return dfloor;
    }
    else if (cfloor < dfloor)
    {
        elevator.status = UP;
        printk(KERN_INFO "movement: Elevator moving UP\n");
        new_floor = cfloor + 1;
    }
    else
    {
        elevator.status = DOWN;
        printk(KERN_INFO "movement: Elevator moving DOWN\n");
        new_floor = cfloor - 1;
    }
    msleep(2); // Simulate movement delay
    printk(KERN_INFO "movement: New floor %d\n", new_floor);
    return new_floor; // Return the new floor after movement
}

static int loading(void)
{
    printk(KERN_INFO "loading: Attempting to load passengers\n");
    struct list_head *temp, *dummy;
    struct Passenger *passenger;
    int passenger_weight;

    if (mutex_lock_interruptible(&elevator.lock) == 0)
    {
        list_for_each_safe(temp, dummy, &floors[elevator.current_floor - 1].passengers)
        {
            passenger = list_entry(temp, struct Passenger, struct_lister);
            passenger_weight = passenger_weights[passenger->type]; // Use the lookup array

            printk(KERN_INFO "loading: Checking passenger %d with weight %d\n", passenger->id, passenger_weight);

            // Check if the elevator can accommodate the passenger
            if (elevator.current_weight + passenger_weight <= MAX_WEIGHT && elevator.passenger_count < MAX_PASSENGERS)
            {
                list_move_tail(temp, &elevator.passengers);
                elevator.current_weight += passenger_weight;
                elevator.passenger_count++;
                printk(KERN_INFO "loading: Passenger %d loaded\n", passenger->id);
            }
            else
            {
                // Elevator full or would exceed weight limit
                printk(KERN_INFO "loading: Elevator full or would exceed weight limit. Breaking.\n");
                break;
            }
        }
        mutex_unlock(&elevator.lock);
    }
    return 0;
}

static int unloading(void)
{
    printk(KERN_INFO "unloading: Attempting to unload passengers\n");
    struct list_head *temp, *dummy;
    struct Passenger *p;

    if (mutex_lock_interruptible(&elevator.lock) == 0)
    {
        list_for_each_safe(temp, dummy, &elevator.passengers)
        {
            p = list_entry(temp, struct Passenger, struct_lister);

            printk(KERN_INFO "unloading: Checking passenger %d for unloading\n", p->id);

            if (p->destination_floor == elevator.current_floor)
            {
                // Remove passenger from elevator
                list_del(temp);
                kfree(p);                                              // Free the memory allocated for the passenger
                elevator.current_weight -= passenger_weights[p->type]; // Adjust weight
                elevator.passenger_count--;
                helped++; // Increment serviced counter
                printk(KERN_INFO "unloading: Passenger %d unloaded\n", p->id);
            }
        }
        mutex_unlock(&elevator.lock);
    }
    return 0;
}

static int elevator_thread_fn(void *data)
{
    printk(KERN_INFO "elevator_thread: Thread started\n");
    while (!kthread_should_stop())
    { // Keep running until the module is unloaded
        printk(KERN_INFO "elevator_thread: Checking elevator status\n");
        if (elevator.status != OFFLINE)
        { // Check if the elevator is operational
            printk(KERN_INFO "elevator_thread: Elevator is operational\n");
            // Movement towards the destination floor if not IDLE
            if (elevator.status == UP || elevator.status == DOWN)
            {
                printk(KERN_INFO "elevator_thread: Elevator moving, current floor: %d\n", elevator.current_floor);
                elevator.current_floor = movement(elevator.current_floor, elevator.target_floor);

                if (elevator.current_floor == elevator.target_floor)
                {
                    // Arrived at the target, switch to LOADING state to unload/load passengers
                    printk(KERN_INFO "elevator_thread: Elevator arrived at target floor: %d\n", elevator.target_floor);
                    elevator.status = LOADING;
                }
            }

            // Loading or unloading passengers if on the target floor
            if (elevator.status == LOADING)
            {
                printk(KERN_INFO "elevator_thread: Loading/Unloading passengers\n");
                unloading();  // Unload passengers first
                loading();    // Load new passengers
                msleep(1000); // Sleep to simulate time passing, adjust as needed

                // Determine next action: stay IDLE, or set a new target floor
                if (!list_empty(&elevator.passengers))
                {
                    // Set target to the destination of the first passenger in the list
                    struct Passenger *first_passenger = list_first_entry(&elevator.passengers, struct Passenger, struct_lister);
                    elevator.target_floor = first_passenger->destination_floor;
                    printk(KERN_INFO "elevator_thread: New target floor set: %d\n", elevator.target_floor);

                    // Decide movement direction based on the new target
                    if (elevator.current_floor < elevator.target_floor)
                    {
                        elevator.status = UP;
                        printk(KERN_INFO "elevator_thread: Elevator status set to UP\n");
                    }
                    else if (elevator.current_floor > elevator.target_floor)
                    {
                        elevator.status = DOWN;
                        printk(KERN_INFO "elevator_thread: Elevator status set to DOWN\n");
                    }
                }
                else
                {
                    // No passengers left to service, remain IDLE or check for passengers waiting
                    if (check_for_waiting_passengers())
                    {
                        elevator.status = LOADING;
                        printk(KERN_INFO "elevator_thread: Passengers waiting, status set to LOADING\n");
                    }
                    else
                    {
                        elevator.status = IDLE;
                        printk(KERN_INFO "elevator_thread: No passengers left, status set to IDLE\n");
                    }
                }
            }
        }
        else
        {
            printk(KERN_INFO "elevator_thread: Elevator is OFFLINE\n");
        }

        msleep(2000); // Sleep to simulate time passing, adjust as needed
    }
    printk(KERN_INFO "elevator_thread: Thread stopped\n");
    return 0;
}

int stop_elevator(void)
{
    printk(KERN_NOTICE "stop_elevator: Function called\n");
    if (mutex_lock_interruptible(&elevator.lock) == 0)
    {
        printk(KERN_INFO "stop_elevator: Acquired lock\n");
        if (elevator.running == 0 && elevator.stopped == 1)
        {
            printk(KERN_INFO "stop_elevator: Elevator already stopped\n");
            mutex_unlock(&elevator.lock);
            return 1;
        }
        elevator.stopped = 1;
        elevator.running = 0;
        printk(KERN_INFO "stop_elevator: Elevator stopped\n");
        mutex_unlock(&elevator.lock);
    }
    return 0;
}

static ssize_t elevator_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
    char buf[10000];
    int len = 0;
    struct Passenger *pass, *temp;
    int i;
    printk(KERN_INFO "elevator_read: READING ELEVATOR STATUS\n\n");
    // Elevator state
    const char *status_str[] = {"OFFLINE", "IDLE", "LOADING", "UP", "DOWN"};
    len += sprintf(buf + len, "Elevator state: %s\n", status_str[elevator.status]);
    printk(KERN_INFO "elevator_read: READING ELEVATOR STATUS %s\n\n", status_str[elevator.status]);

    // Current floor and load
    len += sprintf(buf + len, "Current floor: %d\n", elevator.current_floor);
    printk(KERN_INFO "elevator_read: READING ELEAVATOR STATUS %d\n\n", elevator.current_floor);
    len += sprintf(buf + len, "Current load: %d lbs\n", elevator.current_weight);
    printk(KERN_INFO "elevator_read: READING ELEAVATOR STATUS %d\n\n", elevator.current_weight);

    // List of passengers in the elevator
    len += sprintf(buf + len, "Elevator status: ");
    printk(KERN_INFO "elevator_read: READING ELEAVATOR STATUS\n\n");
    list_for_each_entry_safe(pass, temp, &elevator.passengers, struct_lister)
    {
        printk(KERN_INFO "Passenger ID: %d, Type: %d, Weight: %d, Start Floor: %d, Destination Floor: %d\n",
               pass->id, pass->type, pass->weight, pass->start_floor, pass->destination_floor);
        switch (pass->type)
        {
        case PART_TIME:
            len += sprintf(buf + len, "P%d ", pass->destination_floor);
            break;
        case LAWYER:
            len += sprintf(buf + len, "L%d ", pass->destination_floor);
            break;
        case BOSS:
            len += sprintf(buf + len, "B%d ", pass->destination_floor);
            break;
        case VISITOR:
            len += sprintf(buf + len, "V%d ", pass->destination_floor);
            break;
        default:
            len += sprintf(buf + len, "?%d ", pass->destination_floor);
            break;
        }
    }
    printk(KERN_INFO "elevator_read: READING ELEAVATOR STATUS FINISHED LOOP\n\n");
    len += sprintf(buf + len, "\n");
    // Number of passengers on each floor
    for (i = 0; i < 5; i++)
    {
        len += sprintf(buf + len, "[%c] Floor %d: ", (i == elevator.current_floor ? '*' : ' '), i + 1);
        struct Passenger *floor_pass;
        list_for_each_entry(floor_pass, &floors[i].passengers, struct_lister)
        {
            switch (floor_pass->type)
            {
            case PART_TIME:
                len += sprintf(buf + len, "P%d ", floor_pass->destination_floor);
                break;
            case LAWYER:
                len += sprintf(buf + len, "L%d ", floor_pass->destination_floor);
                break;
            case BOSS:
                len += sprintf(buf + len, "B%d ", floor_pass->destination_floor);
                break;
            case VISITOR:
                len += sprintf(buf + len, "V%d ", floor_pass->destination_floor);
                break;
            default:
                len += sprintf(buf + len, "?%d ", floor_pass->destination_floor);
                break;
            }
        }
        len += sprintf(buf + len, "\n");
    }

    // Number of passengers, passengers waiting, and passengers serviced
    len += sprintf(buf + len, "Number of passengers: %d\n", elevator.passenger_count);
    len += sprintf(buf + len, "Number of passengers waiting: %d\n", wait);
    len += sprintf(buf + len, "Number of passengers serviced: %d\n", helped);

    printk(KERN_INFO "elevator_read: buf=%s\n", buf);
    printk(KERN_INFO "elevator_read: len=%d\n", len);
    printk(KERN_INFO "elevator_read: count=%zu, ppos=%lld\n", count, *ppos);

    // Use simple_read_from_buffer
    return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

static const struct proc_ops elevator_fops = {
    .proc_read = elevator_read,
};
static int __init elevator_init(void)
{
    int i;

    STUB_start_elevator = start_elevator;
    STUB_issue_request = issue_request;
    STUB_stop_elevator = stop_elevator;
    // Initialize elevator
    elevator.current_floor = 1; // Assuming ground floor is 1
    elevator.current_weight = 0;
    elevator.passenger_count = 0;
    elevator.status = OFFLINE;
    elevator.target_floor = 1; // Set initial target floor
    mutex_init(&elevator.lock);

    // Initialize floors
    for (i = 0; i < 5; i++)
    {
        mutex_init(&floors[i].lock);
        INIT_LIST_HEAD(&floors[i].passengers);
    }

    // Create proc entry
    elevator_entry = proc_create(ENTRY_NAME, 0666, NULL, &elevator_fops);
    if (!elevator_entry)
    {
        printk(KERN_ALERT "Error creating proc entry\n");
        return -ENOMEM;
    }

    // Start elevator thread
    elevator_thread = kthread_run(elevator_thread_fn, NULL, "elevator_thread");
    if (IS_ERR(elevator_thread))
    {
        printk(KERN_ALERT "Error creating elevator thread\n");
        return PTR_ERR(elevator_thread);
    }

    printk(KERN_INFO "Elevator module loaded\n");
    return 0;
}
static void elevator_exit(void)
{
    struct list_head *temp, *dummy;
    struct Passenger *passenger;
    int i;

    // Stop the elevator thread
    if (elevator_thread)
    {
        kthread_stop(elevator_thread);
    }

    // Free waiting passengers on each floor
    for (i = 0; i < 5; i++)
    {
        mutex_lock(&floors[i].lock);
        list_for_each_safe(temp, dummy, &floors[i].passengers)
        {
            passenger = list_entry(temp, struct Passenger, struct_lister);
            list_del(temp);
            kfree(passenger);
        }
        mutex_unlock(&floors[i].lock);
        mutex_destroy(&floors[i].lock); // Destroy the floor mutex
    }

    // Clear any passengers currently in the elevator
    mutex_lock(&elevator.lock);
    list_for_each_safe(temp, dummy, &elevator.passengers)
    {
        passenger = list_entry(temp, struct Passenger, struct_lister);
        list_del(temp);
        kfree(passenger);
    }
    mutex_unlock(&elevator.lock);

    // Free elevator resources and destroy its mutex
    mutex_destroy(&elevator.lock);

    // Remove the proc entry if it exists
    if (elevator_entry)
    {
        proc_remove(elevator_entry);
    }

    printk(KERN_INFO "Elevator module exited\n");

    STUB_start_elevator = NULL;
    STUB_issue_request = NULL;
    STUB_stop_elevator = NULL;
    // Remove the proc entry if it exists
    if (elevator_entry)
    {
        proc_remove(elevator_entry);
    }

    printk(KERN_INFO "Elevator module \n");
}

module_init(elevator_init);
module_exit(elevator_exit);
