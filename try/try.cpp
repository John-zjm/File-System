#include "pch.h"
#include "try.h"
using namespace std;

//全局变量定义
const int Superblock_StartAddr = 0;																//超级块 偏移地址,占一个磁盘块
const int InodeBitmap_StartAddr = 1 * BLOCK_SIZE;													//inode位图 偏移地址，占512个磁盘块，最多监控 524288 个inode的状态
const int BlockBitmap_StartAddr = InodeBitmap_StartAddr + INODE_NUM /(sizeof(bool)*1024) * BLOCK_SIZE;							//block位图 偏移地址，占1024个磁盘块，最多监控 1048576 个磁盘块（1g）的状态
const int Inode_StartAddr = BlockBitmap_StartAddr + BLOCK_NUM /(BLOCK_SIZE *sizeof(bool)) * BLOCK_SIZE;								//inode节点区 偏移地址，占 INODE_NUM/(BLOCK_SIZE/INODE_SIZE) 个磁盘块
const int Block_StartAddr = Inode_StartAddr + INODE_NUM / (BLOCK_SIZE / INODE_SIZE) * BLOCK_SIZE;	//block数据区 偏移地址 ，占 INODE_NUM 个磁盘块

const int Sum_Size = Block_StartAddr + BLOCK_NUM * BLOCK_SIZE;									//虚拟磁盘文件大小

//单个文件最大大小
const int File_Max_Size = 10 * BLOCK_SIZE +														//10个直接块
BLOCK_SIZE / sizeof(int) * BLOCK_SIZE +								//一级间接块
(BLOCK_SIZE / sizeof(int))*(BLOCK_SIZE / sizeof(int)) * BLOCK_SIZE +		//二级间接块
(BLOCK_SIZE / sizeof(int))*(BLOCK_SIZE / sizeof(int)) *(BLOCK_SIZE / sizeof(int))* BLOCK_SIZE; // 三级间接块

int Root_Dir_Addr;							//根目录inode地址
int Cur_Dir_Addr;							//当前目录
char Cur_Dir_Name[310];						//当前目录名

FILE* fw;									//虚拟磁盘文件 写文件指针
FILE* fr;									//虚拟磁盘文件 读文件指针
SuperBlock *superblock = new SuperBlock;	//超级块指针
bool inode_bitmap[INODE_NUM];				//inode位图
bool block_bitmap[BLOCK_NUM];				//磁盘块位图

int main()
{
	printf("%d\n", INODE_NUM / (BLOCK_SIZE / INODE_SIZE));

	//打开虚拟磁盘文件 
	if ((fr = fopen(FILESYSNAME, "rb")) == NULL)
	{	//只读打开虚拟磁盘文件（二进制文件）
		//虚拟磁盘文件不存在，创建一个
		fw = fopen(FILESYSNAME, "wb");	//只写打开虚拟磁盘文件（二进制文件）
		if (fw == NULL) {
			printf("虚拟磁盘文件打开失败\n");
			return 0;	//打开文件失败
		}
		fr = fopen(FILESYSNAME, "rb");	//现在可以打开了

		//根目录inode地址 ，当前目录地址和名字
		Root_Dir_Addr = Inode_StartAddr;	//第一个inode地址
		Cur_Dir_Addr = Root_Dir_Addr;
		strcpy(Cur_Dir_Name, "/");

		printf("文件系统正在格式化……\n");
		if (!Format()) {
			printf("文件系统格式化失败\n");
			return 0;
		}
		printf("格式化完成\n");
		printf("按任意键进行第一次登陆\n");
		system("pause");
		system("cls");

		if (!Install()) {
			printf("安装文件系统失败\n");
			return 0;
		}
	}
	else
	{
		Install();
		void info();
	}
}

