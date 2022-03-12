/*
 * 2022年春季学期操作系统课程项目一
 *
 * Minux上实现一个简单的shell
 *
 * name: 杨舜  ID: 10205501415
 * 
 * created_date: 2022-2-28
 * 
 * update_log: 1. 2022-2-28  基本框架的搭建，完成cd、quit、exit等内置命令
 *             2. 2022-2-29  完善与并测试cd与ls命令
 *             3. 2022-2-29  测试完成execvp()函数成功实现调用其他文件以及执行可执行文件
 *             4. 2022-3-3   进行错误类型封装，但是封装有bug，程序停在了调用cd、quit函数处无法进行后续命令的执行
 *             5. 2022-3-7   修改上次错误类型封装的bug，运行并测试基本操作
 *             6. 2022-3-7   重定向功能的实现操作
 *             7. 2022-3-9   管道功能的实现
 *             8. 2022-3-9   后台运行方式实现(未测试)
 *             9. 2022-3-10  后台运行（测试）
 *             10.2022-3-10  history命令实现
 *             11.2022-3-10  mytop命令实现
 * 
 * DDL: 
 * 
 * finish_date: 2022-3-10 19:39:45
 * 
 * handout_date: 
 *
*/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<ctype.h>
#include<signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include<pwd.h>
#include <limits.h>
#include <termcap.h>
#include <termios.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <assert.h>
// #include <sys/ioc_tty.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>

#include <minix/com.h>
#include <minix/config.h>
#include <minix/type.h>
#include <minix/endpoint.h>
#include <minix/const.h>
#include <minix/u64.h>
#include <paths.h>
#include <minix/procfs.h>

/* Constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */

#define FILE_NAME "/home/history/history.txt" /* 文件路径 */


/* return status settings */
enum{
    /* normal return */
    NORMAL_RETURN,

    /* not built-in command */
    NOT_BUILTIN,

    /* is redirection */
    IS_REDIRECTION,

    /* fork error */
    ERROR_FORK,
    
    /* command not found */
    ERROR_COMMAND,

    /* lost parameter */
    ERROR_LOST_PARAMETER,

    /* too many parameters */
    ERROR_TOO_MANY_PARAMETER,

    /* system error*/
    ERROR_SYSTEM,

    /* path not found */
    ERRRE_PATH_NOT_FOUND,

    /* file not exist */
    ERROR_FILE_NOT_EXIST,

    /* 重定向错误 */
    /* 重定向没有输出文件 */
    ERROR_REDIRECTION_NO_OUT,

    /* 重定向没有输入文件 */
    ERROR_REDIRECTION_NO_IN,

    /* 重定向没有追加文件 */
    ERROR_REDIRECTION_NO_APPEND,

    /* 重定向输入超过1 */
    ERROR_MANY_IN,

    /* 重定向输出超过1 */
    ERROR_MANY_OUT,

    /* 重定向输出超过1 */
    ERROR_MANY_APPEND,

    /* 管道错误 */
    /* 管道生成错误 */
    ERROR_PIPE,

    /* 管道命令没有后续命令 */
    ERROR_PIPE_NO_PARAMETER,

    /* histroy n 命令没有参数 */
    ERROR_HISTORY_NO_PARAMETER,

};

/* Global variables */
char prompt[]="myshell";
extern char **environ;  /* 外部引入环境 */
char username[MAXLINE]; /* shell用户 */
char hostname[MAXLINE]; /* 主机IP */ 
char curPath[MAXLINE];  /* 工作路径 */


int workBack = 0;       /* work on backgroung */


/* 解析输入以空格划分输入,存入argv[]并返回数量 */
int parseline(const char *cmdline, char **argv);

/* 执行输入 */
void eval(char *cmdline);

/* 内置指令 */
int buildin_cmd(char **argv,int argc);

/* 获取用户、主机、工作路径 */
void getUsername();
void getHostname();
int getCurWorkDir();

/* 信号处理函数 */
static void mask_ctrl_c(int signo);

/* 固定的一些命令 */
int __cd__(char **argv,int argc);

/* history 命令 */
int historyN(char **argv,int argc);

/* 判断并执行有重定向命令 */
int isRedirect(char **argv, int argc);

/* 管道功能 */
int pipeCommand(char **argv, int argc);

/* mytop函数部分实现 */
#define USED 0x1
#define IS_TASK 0x2
#define IS_SYSTEM 0x4
#define BLOCKED 0x8
#define PSINFO_VERSION 0
#define M 256

#define STATE_RUN 'R'
const char *cputimenames[] = {"user", "ipc", "kernelcall"};
#define CPUTIMENAMES ((sizeof(cputimenames)) / (sizeof(cputimenames[0]))) //恒等于3
#define CPUTIME(m, i) (m & (1 << (i)))                                    //保留第几位
char history[M][M];
int n_his = 0;
char *path = NULL;
unsigned int nr_procs, nr_tasks;
int slot = -1;
int nr_total;

struct proc
{
    int p_flags;
    endpoint_t p_endpoint;           //端点
    pid_t p_pid;                     //进程号
    u64_t p_cpucycles[CPUTIMENAMES]; //CPU周期
    int p_priority;                  //动态优先级
    endpoint_t p_blocked;            //阻塞状态
    time_t p_user_time;              //用户时间
    vir_bytes p_memory;              //内存
    uid_t p_effuid;                  //有效用户ID
    int p_nice;                      //静态优先级
    char p_name[PROC_NAME_LEN + 1];  //名字
};

struct proc *proc = NULL, *prev_proc = NULL;

struct tp
{
    struct proc *p;
    u64_t ticks;
};

int mytop();
void getkinfo();
void get_procs();
void parse_dir();
void parse_file(pid_t pid);
u64_t cputicks(struct proc *p1, struct proc *p2, int timemode);
void print_procs(struct proc *proc1, struct proc *proc2, int cputimemode);



/* 错误封装fork函数 */
pid_t Fork(void) {
    pid_t pid;
    if ((pid = fork()) < 0)
    {
        fprintf(stderr,"Fork error\n");
    }
    return pid;
}

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) {
    char cmdline[MAXLINE];
    int workDir = getCurWorkDir();
    if (workDir == ERROR_SYSTEM)
    {
        fprintf(stderr,"Error: System error while getting current work directory.\n");
        _exit(ERROR_SYSTEM);
    }
    getUsername();
    getHostname();
    
    /* 信号处理函数 */
    signal(SIGINT,mask_ctrl_c);
    FILE *initp = NULL;
    initp = fopen(FILE_NAME,"w+");
    char* inicomment = "";
    fputs(inicomment,initp);
    fclose(initp);
    while(1) //简单while循环
    {
        workDir = getCurWorkDir();
        if (workDir == ERROR_SYSTEM)
        {
            fprintf(stderr,"Error: System error while getting current work directory.\n");
            _exit(ERROR_SYSTEM);
        }
        printf("[(%s)|%s@%s:%s]# ", prompt, username, hostname,curPath); 
        fflush(stdout);
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)){
            fprintf(stderr,"fgets error\n");
        }
        if (feof(stdin)){
            fflush(stdout);
            exit(0);
        }
        FILE* fp = NULL; // 文件指针
	    // char* szAppendStr = cmdline;
        // printf("%s\n",cmdline);
        // printf("%p\n",fp);
        // printf("%s\n",szAppendStr);
	    /* 以附加方式打开可读/写的文件, 如果没有此文件则会进行创建，然后以附加方式打开可读/写的文件 */
	    fp = fopen(FILE_NAME, "a+");
        // printf("%p\n",fp);
	    /* 打开文件失败 */
	    if (fp == NULL)
		    exit(-1);
	    /* 将追加内容写入文件指针当前的位置*/
	    fputs(cmdline, fp);
	    /* 关闭打开的文件*/
	    fclose(fp);
        eval(cmdline);
        fflush(stdout);
    }
}

void eval(char *cmdline) {
    char *argv[MAXARGS];
    int argc = parseline(cmdline,argv);
    // printf("checkpoint1\n");
    int statu;
    if ((statu = buildin_cmd(argv,argc)) == NORMAL_RETURN) /* 执行quit,cd等内置函数 */
    {
        return;
    } else if (statu == ERROR_LOST_PARAMETER){
        fprintf(stderr,"%s command: 缺少参数 \n",argv[0]);
        return;
    } else if (statu == ERROR_TOO_MANY_PARAMETER){
        fprintf(stderr,"%s command: 输入参数过多\n",argv[0]);
        return;
    } else if (statu == ERRRE_PATH_NOT_FOUND){
        fprintf(stderr,"%s command: path not found\n",argv[0]);
        return;
    } else if (statu == ERROR_HISTORY_NO_PARAMETER){
        fprintf(stderr,"%s command: 缺少参数\n",argv[0]);
        return;
    }

    int status;

    pid_t pid;
    sigset_t mask, prev;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &prev); /* 同时为子进程和父进程阻塞信号 */

    pid = fork();
    if (pid == 0){ /* 子进程 */
        
        int child_new_gid = setpgid(0, 0);
        sigprocmask(SIG_SETMASK, &prev, NULL); /*子进程解除阻塞*/
        if (!child_new_gid) /* 为子进程创建新的进程组 setpgid */
            {
                /* 获取标准输入、输出的文件标识符 */
                int inFds = dup(STDIN_FILENO);
		        int outFds = dup(STDOUT_FILENO);

		        int result = pipeCommand(argv,argc);
		
		        /* 还原标准输入、输出重定向 */
		        dup2(inFds, STDIN_FILENO);
		        dup2(outFds, STDOUT_FILENO);
		        exit(result);
            }
        
    }else{ /* 父进程 */
        if(workBack){
            sigprocmask(SIG_SETMASK, &prev, NULL);
            printf("(%d) %s working on background\n",pid, cmdline);
            return;
        }else{
            sigprocmask(SIG_SETMASK, &prev, NULL);
            waitpid(pid, &status, 0);
        if (status == NORMAL_RETURN){
            return;
        } else if (status == ERROR_FILE_NOT_EXIST) {
            fprintf(stderr,"重定向文件不存在\n");
            return;
        } else if (status == ERROR_REDIRECTION_NO_IN) {
            fprintf(stderr,"没有重定向输入文件\n");
            return;
        } else if (status == ERROR_REDIRECTION_NO_IN) {
            fprintf(stderr,"没有重定向输出文件\n");
            return;
        } else if (status == ERROR_REDIRECTION_NO_IN) {
            fprintf(stderr,"没有重定向追加文件\n");
            return;
        } else if (status == ERROR_MANY_IN) {
            fprintf(stderr,"输入重定向符超过一个\n");
            return;
        } else if (status == ERROR_MANY_OUT) {
            fprintf(stderr,"输出重定向符超过一个\n");
            return;
        } else if (status == ERROR_MANY_APPEND) {
            fprintf(stderr,"追加重定向符超过一个\n");
            return;
        } else if (status == ERROR_PIPE) {
            fprintf(stderr,"管道生成失败\n");
            return;
        } else if (status == ERROR_PIPE_NO_PARAMETER){
            fprintf(stderr,"管道命令没有参数\n");
            return;
        }
        }
    }
}

int parseline(const char *cmdline, char **argv) {
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */                    

    strcpy(buf, cmdline);
    // printf("buf:%s",buf);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
	delim = strchr(buf, ' ');

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;
	delim = strchr(buf, ' ');
    }
    argv[argc] = NULL;

    if (*argv[argc-1] == '&')
    {
        workBack = 1;
        argv[--argc] = NULL;
    }
	return argc; /* Get the count of argv */
}

int buildin_cmd(char **argv,int argc) {
    int status = NOT_BUILTIN;
    if((!strcmp(argv[0],"quit"))||(!strcmp(argv[0],"exit"))){
        _exit(NORMAL_RETURN);
    }
    if(!strcmp(argv[0],"cd")){
        status = NORMAL_RETURN;
        status = __cd__(argv,argc);
        return status;
    }
    if(!strcmp(argv[0],"history")){
        status = NORMAL_RETURN;
        status = historyN(argv,argc);
        return status;
    }
    if(!strcmp(argv[0],"mytop")){
        status = NORMAL_RETURN;
        status = mytop();
        return status;
    }
    return status;
}

void getUsername() { // 获取当前登录的用户名
	struct passwd* pwd = getpwuid(getuid());
	strcpy(username, pwd->pw_name);
}

void getHostname() { // 获取主机名
	gethostname(hostname, MAXLINE);
}

int getCurWorkDir() { // 获取当前的工作目录
	char* result = getcwd(curPath, MAXLINE);
	if (result == NULL)
		return ERROR_SYSTEM;
	else return NORMAL_RETURN;
}

int __cd__(char **argv,int argc) {
    int status = NORMAL_RETURN;
    if (argc<2){
        status = ERROR_LOST_PARAMETER;
    }
    else if (argc>2){
        status = ERROR_TOO_MANY_PARAMETER;
    } else {
        int ret = chdir(argv[1]);
        if (ret) {
            status = ERRRE_PATH_NOT_FOUND;
        }
    }
    return status;   
}

int isRedirect(char **argv, int argc ) {
    int status = NORMAL_RETURN;
    int inNum = 0;
    int outNum = 0;
    int appendNum = 0;
    int endIndex;
    char *infile, *outfile;
    char *appendfile;
    endIndex = argc;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i],"<")) {
            inNum++;
            if (i+1 < argc) {
                infile = argv[i+1];
            } else { 
                return ERROR_REDIRECTION_NO_IN;
            }
            if ((endIndex) == argc) {
                endIndex = i;
            }
        } else if (!strcmp(argv[i],">")) {
            outNum++;
            if (i+1 < argc) {
                outfile = argv[i+1];
            } else { 
                return ERROR_REDIRECTION_NO_OUT;
            }		
            if ((endIndex) == argc) {
                endIndex = i;
            }
        } else if (!strcmp(argv[i],">>")) {
            appendNum++;
            if (i+1 < argc) {
                appendfile = argv[i+1];
            } else {
                return ERROR_REDIRECTION_NO_APPEND;
            }
            if ((endIndex) == argc) {
                endIndex = i;
            }
        }
    }
    /* 重定向判断 */
    if (inNum == 1) { /* 判断输入文件是否存在 */
		FILE* fp = fopen(infile, "r");
		if (fp == NULL) {
            return ERROR_FILE_NOT_EXIST;
        }
		fclose(fp);
	}
	if (inNum > 1) { /* 输入重定向符超过一个 */
		return ERROR_MANY_IN;
	} else if (outNum > 1) { /* 输出重定向符超过一个 */
		return ERROR_MANY_OUT;
	} else if (appendNum > 1) { /* 追加重定向符超过一个 */
        return ERROR_MANY_APPEND;
    }
    
    pid_t pid;

    if ((pid = Fork()) == 0) { /* 子进程 */
        if (inNum == 1) {
            freopen(infile, "r", stdin);
        }
        if (outNum == 1) {
            freopen(outfile, "w", stdout);
        }
        if (appendNum == 1){
            freopen(appendfile, "a", stdout);
        }
        char *argv_tmp[MAXARGS];
        for (int i = 0; i < endIndex; i++) {
            argv_tmp[i] = argv[i];
        }
        argv_tmp[endIndex] = NULL;
        execvp(argv_tmp[0],argv_tmp);
        exit(errno);
    }
    else {
        int ret;
        waitpid(pid, &ret, 0);
        // printf("%d\n",status);
        if(ret) {
            fprintf(stderr, "%s: Command not found\n", argv[0]);
        }
        fflush(stdout);
    }
    return status;

}

int pipeCommand(char **argv, int argc) { // 管道命令
    int pipeIndex = -1;
    for (int i=0; i<argc; ++i) {
		if (!strcmp(argv[i], "|")) {
			pipeIndex = i;
			break;
		}
	}
    if (pipeIndex == -1) {
        return isRedirect(argv, argc);
    } else {
        if (pipeIndex+1 == argc) { // 管道命令'|'后续没有指令，参数缺失
            return ERROR_PIPE_NO_PARAMETER;
        }
        else {
            char **preCommand;    /* 管道命令前的命令 */
            char **lastCommand;   /* 管道命令后的命令 */
            int preCount = -1;
            int lastCount = -1;
            int i = 0;
            int j = 0;
            int k = 0;
            for (i = 0; i < pipeIndex; i++)
            {
                strcpy(preCommand[i],argv[i]);
            }
            preCommand[i] = NULL;
            preCount = i;
            for (j = pipeIndex+1, k = 0; j < argc; j++, k++)
            {
                strcpy(lastCommand[k],argv[j]);
            }
            lastCommand[k] = NULL; 
            lastCount = k;
            int fds[2];
            if (pipe(fds) == -1) { /* 创建管道 */
	    	return ERROR_PIPE;
	        }
            int result = NORMAL_RETURN;
            pid_t pid = Fork();
	        if (pid == 0) { // 子进程执行单个命令
		        close(fds[0]);
		        dup2(fds[1], STDOUT_FILENO); // 将标准输出重定向到fds[1]
		        close(fds[1]);
		        
                /* 执行“|”命令 */
                result = isRedirect(preCommand, preCount);
		        exit(result);
	        } else { // 父进程递归执行后续命令
		        int status;
		        waitpid(pid, &status, 0);
                if (status != NORMAL_RETURN){
                    return status;
                }
		        close(fds[1]);
		        dup2(fds[0], STDIN_FILENO); // 将标准输入重定向到fds[0]
		        close(fds[0]);
		        /* 执行“|”后的命令 */
                result = isRedirect(lastCommand,lastCount);
	        }
            return result;
        }   
    }
}

int historyN(char **argv,int argc){
    int status = NORMAL_RETURN;
    if (argc == 1){
        return ERROR_HISTORY_NO_PARAMETER;
    }
    else{
        int N = atoi(argv[1]);
    // printf("%d\n",N);
    // printf("check point 1\n"); 
        char s[1024];
        FILE *fp;
        int lines=0;
        fp=fopen(FILE_NAME, "r");
        // printf("check point 1\n");
        int readSta = 0;
        if(fp)
        {
            while((fgets(s,1024,fp)) != NULL)
                if(s[strlen(s)-1]=='\n') lines++;
            // printf("%d\n",lines);
            readSta = lines - N;
            lines = 0;
            // printf("check point 1\n");
            fclose(fp);
        }
        // printf("%d\n",readSta);
        fp=fopen(FILE_NAME, "r");
        if (fp)
        {
            while((fgets(s,1024,fp)) != NULL){
                // printf("check point 1\n");
                if(s[strlen(s)-1]=='\n') lines++;
                if (lines == readSta) break;
            }
            while ((fgets(s,1024,fp)) != NULL)
                printf("%s",s);
            fclose(fp);
        }
        return status;
    }
    return status;
}

int mytop(){
    FILE* memp;
    char mems[30];
    memp = fopen("/proc/meminfo","r");
    fgets(mems,30,memp);
    // printf("%s\n",mems);
    int pageSize;
    int toTal;
    int freePage;
    int largestPage;
    int cachedPage;
    sscanf(mems,"%d %d %d %d %d",&pageSize,&toTal,&freePage,&largestPage,&cachedPage);
    printf("total memery size: %d\n",(pageSize*toTal)/1024);
    printf("free memery size: %d\n",(freePage*pageSize)/1024);
    printf("cache memery size: %d\n",(cachedPage*pageSize)/1024);
    // printf("total CPU usage: \n");
    int cputimemode = 1;
    getkinfo();
    get_procs();
        if (prev_proc == NULL)
        {
            get_procs();//得到proc
        }
        print_procs(prev_proc, proc, cputimemode);
    return NORMAL_RETURN;
}

u64_t cputicks(struct proc *p1, struct proc *p2, int timemode)
{
    int i;
    u64_t t = 0;
    for (i = 0; i < CPUTIMENAMES; i++)
    {
        if (!CPUTIME(timemode, i))
        {
            continue;
        }
        if (p1->p_endpoint == p2->p_endpoint)
        {
            t = t + p2->p_cpucycles[i] - p1->p_cpucycles[i];
        }
        else
        {
            t = t + p2->p_cpucycles[i];
        }
    }
    return t;
}

void getkinfo()//读取进程数
{
    FILE *fp;
    if ((fp = fopen("/proc/kinfo", "r")) == NULL)
    {
        fprintf(stderr, "opening /proc/kinfo failed\n");
        exit(1);
    }

    if (fscanf(fp, "%u %u", &nr_procs, &nr_tasks) != 2)
    {
        fprintf(stderr, "reading from /proc/kinfo failed");
        exit(1);
    }

    fclose(fp);

    //nr_total是一个全局变量
    nr_total = (int)(nr_procs + nr_tasks);
}

void get_procs()
{
    struct proc *p;
    int i;
    //交换了prev_proc&proc
    p = prev_proc;
    prev_proc = proc;
    proc = p;

    if (proc == NULL)
    {
        //proc是struct proc的集合，申请了
        //nr_total个proc的空间
        proc = malloc(nr_total * sizeof(proc[0])); //struct proc的大小
        if (proc == NULL)
        {
            fprintf(stderr, "Out of memory!\n");
            exit(0);
        }
    }

    for (i = 0; i < nr_total; i++)
    {
        proc[i].p_flags = 0; 
    }

    parse_dir();
}

void parse_dir()
{
    DIR *p_dir;
    struct dirent *p_ent; 
    pid_t pid;
    char *end; //指向第一个不可转换的字符位置的指针

    if ((p_dir = opendir("/proc")) == NULL)
    {
        perror("opendir on /proc");
        exit(0);
    }

    //读取目录下的每一个文件信息
    for (p_ent = readdir(p_dir); p_ent != NULL; p_ent = readdir(p_dir))
    {
        //long int strtol (const char* str, char** endptr, int base);
        //将字符串转化为长整数，endptr第一个不可转换的位置的字符指针，base要转换的进制
        //合法字符为0x1-0x9
        pid = strtol(p_ent->d_name, &end, 10);
        //由文件名获取进程号
        //pid由文件名转换得来
        //ASCII码对照表，NULL的值为0
        if (!end[0] && pid != 0)
        {
            parse_file(pid);
        }
    }
    closedir(p_dir);
}

void parse_file(pid_t pid)
{
    //PATH_MAX定义在头文件<limits.h>，对路径名长度的限制
    char path[PATH_MAX], name[256], type, state;
    int version, endpt, effuid;         //版本，端点，有效用户ID
    unsigned long cycles_hi, cycles_lo; //高周期，低周期
    FILE *fp;
    struct proc *p;
    int i;
    //将proc/pid/psinfo路径写入path
    sprintf(path, "/proc/%d/psinfo", pid);

    if ((fp = fopen(path, "r")) == NULL)
    {
        return;
    }

    if (fscanf(fp, "%d", &version) != 1)
    {
        fclose(fp);
        return;
    }

    if (version != PSINFO_VERSION)
    {
        fputs("procfs version mismatch!\n", stderr);
        exit(1);
    }

    if (fscanf(fp, " %c %d", &type, &endpt) != 2)
    {
        fclose(fp);
        return;
    }

    slot++; //顺序取出每个proc让所有task的slot不冲突

    if (slot < 0 || slot >= nr_total)
    {
        fprintf(stderr, "mytop:unreasonable endpoint number %d\n", endpt);
        fclose(fp);
        return;
    }

    p = &proc[slot]; //取得对应的struct proc

    if (type == TYPE_TASK)
    {
        p->p_flags |= IS_TASK; //0x2 倒数第二位标记为1
    }
    else if (type == TYPE_SYSTEM)
    {
        p->p_flags |= IS_SYSTEM; //0x4 倒数第三位标记为1
    }
    p->p_endpoint = endpt;
    p->p_pid = pid;
    //%*u添加了*后表示文本读入后不赋给任何变量
    if (fscanf(fp, " %255s %c %d %d %llu %*u %lu %lu",name, &state, &p->p_blocked, &p->p_priority,&p->p_user_time, &cycles_hi, &cycles_lo) != 7)
    {
        fclose(fp);
        return;
    }

    //复制src字符串到dest中，大小由tn决定
    strncpy(p->p_name, name, sizeof(p->p_name) - 1);
    p->p_name[sizeof(p->p_name) - 1] = 0;

    if (state != STATE_RUN)
    {
        p->p_flags |= BLOCKED; //0x8 倒数第四位标记为1
    }

    //user的CPU周期
    p->p_cpucycles[0] = make64(cycles_lo, cycles_hi);
    p->p_flags |= USED; //最低位标记位1

    fclose(fp);
}

void print_procs(struct proc *proc1, struct proc *proc2, int cputimemode)
{
	int p, nprocs;
	u64_t idleticks = 0;
	u64_t kernelticks = 0;
	u64_t systemticks = 0;
	u64_t userticks = 0;
	u64_t total_ticks = 0;
	int blockedseen = 0;
	static struct tp *tick_procs = NULL;

	if (tick_procs == NULL) {
		tick_procs = malloc(nr_total * sizeof(tick_procs[0]));

		if (tick_procs == NULL) {
			fprintf(stderr, "Out of memory!\n");
			exit(1);
		}
	}

	for(p = nprocs = 0; p < nr_total; p++) {
		u64_t uticks;
		if(!(proc2[p].p_flags & USED))
			continue;
		tick_procs[nprocs].p = proc2 + p;
		tick_procs[nprocs].ticks = cputicks(&proc1[p], &proc2[p], cputimemode);
		uticks = cputicks(&proc1[p], &proc2[p], 1);
		total_ticks = total_ticks + uticks;
		if(p-NR_TASKS == IDLE) {
			idleticks = uticks;
			continue;
		}
		if(p-NR_TASKS == KERNEL) {
			kernelticks = uticks;
		}
		if(!(proc2[p].p_flags & IS_TASK)) {
			if(proc2[p].p_flags & IS_SYSTEM)
				systemticks = systemticks + tick_procs[nprocs].ticks;
			else
				userticks = userticks + tick_procs[nprocs].ticks;
		}

		nprocs++;
	}

	if (total_ticks == 0)
		return;

	// qsort(tick_procs, nprocs, sizeof(tick_procs[0]), cmp_procs);
    // printf("total_ticks:%6.2llu",total_ticks);
    printf("CPU states: %6.2f%% user, ", 100.0 * userticks / total_ticks);
    printf("%6.2f%% system, ", 100.0 * systemticks / total_ticks);
    printf("%6.2f%% kernel, ", 100.0 * kernelticks / total_ticks);
    printf("%6.2f%% idle\n", 100.0 * idleticks / total_ticks);
}

static void mask_ctrl_c(int signo)
{
	printf("\n");
    int workDir = getCurWorkDir();
	workDir = getCurWorkDir();
    getHostname();
    getUsername();
    if (workDir == ERROR_SYSTEM)
    {
        fprintf(stderr,"Error: System error while getting current work directory.\n");
        _exit(ERROR_SYSTEM);
    }
    printf("[(%s)|%s@%s:%s]# ", prompt, username, hostname,curPath); 
	fflush(stdout);
}      