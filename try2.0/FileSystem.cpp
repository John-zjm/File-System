#include "pch.h"
#include "FileSystem.h"

bool inode_bitmap[INODE_NUM];																						//inode位图
bool block_bitmap[BLOCK_NUM];																						//磁盘块位图

FileSystem::FileSystem(FILE *w, FILE *r, bool is_mount):fw(w),fr(r)
{
	super_block_ = new SuperBlock;
	error_ = 0;
	if (is_mount == 0)
	{
		//根目录inode地址 ，当前目录地址和名字	
		printf("文件系统正在格式化……\n");
		Format();
		root_directory_address_ = kInodeStartAddress;			//第一个inode地址
		current_directory_address_ = root_directory_address_;
		current_directory_path_ = "/";
		if (error_ != 0)
			return;
		printf("格式化完成\n");
		printf("按任意键进行第一次登陆\n");
		system("pause");
		system("cls");
		Open();//打开文件系统
	}
	else 
	{
		Open();//打开文件系统
	}
}

FileSystem::~FileSystem()
{
}

void FileSystem::Format()	//格式化一个虚拟磁盘文件
{
	int i, j;

	//初始化超级块
	super_block_->s_inode_num = INODE_NUM;
	super_block_->s_block_num = BLOCK_NUM;
	super_block_->s_super_block_size = sizeof(SuperBlock);//568
	super_block_->s_inode_size = INODE_SIZE;
	super_block_->s_block_size = BLOCK_SIZE;
	super_block_->s_free_inode_num = INODE_NUM;
	super_block_->s_free_block_num = BLOCK_NUM;
	super_block_->s_blocks_per_group = BLOCKS_PER_GROUP;
	super_block_->s_free_addr = kBlockStartAddress;	//空闲块堆栈指针为第一块block
	super_block_->s_super_block_start_address = kSuperBlockStartAddress;
	super_block_->s_block_bitmap_start_address = kBlockBitmapStartAddress;
	super_block_->s_inode_bitmap_start_address = kInodeBitmapStartAddress;
	super_block_->s_block_start_address = kBlockStartAddress;
	super_block_->s_inode_start_address = kInodeStartAddress;
	//空闲块堆栈在后面赋值

	//初始化inode位图
	memset(inode_bitmap, 0, sizeof(inode_bitmap));
	//初始化block位图
	memset(block_bitmap, 0, sizeof(block_bitmap));
	//初始化磁盘块区，根据成组链接法组织	
	i = BLOCK_NUM / BLOCKS_PER_GROUP - 1;
	for (; i >= 0; i--)
	{	//一共INODE_NUM/BLOCKS_PER_GROUP 8192 组，一组FREESTACKNUM（128）个磁盘块 ，第一个磁盘块作为索引
		if (i == BLOCK_NUM / BLOCKS_PER_GROUP - 1)
			super_block_->s_free[0] = -1;	//没有下一个空闲块了
		else
			super_block_->s_free[0] = kBlockStartAddress + (i + 1)*BLOCKS_PER_GROUP*BLOCK_SIZE;	//指向下一个空闲块
		for (j = 1; j < BLOCKS_PER_GROUP; j++)
		{
			super_block_->s_free[j] = kBlockStartAddress + (i*BLOCKS_PER_GROUP + j)*BLOCK_SIZE;
		}
		fseek(fw, kBlockStartAddress + i * BLOCKS_PER_GROUP*BLOCK_SIZE, SEEK_SET);
		fwrite(super_block_->s_free, sizeof(super_block_->s_free), 1, fw);	//写入这个磁盘块，128*字节
		super_block_->s_free_block_num--;
		block_bitmap[i * BLOCKS_PER_GROUP] = 1;
	}

	//超级块写入到虚拟磁盘文件
	fseek(fw, kSuperBlockStartAddress, SEEK_SET);
	fwrite(super_block_, sizeof(SuperBlock), 1, fw);
	fflush(fw);//强迫将缓冲区内的数据写回参数stream 指定的文件中
	//写入inode位图
	fseek(fw, kInodeBitmapStartAddress, SEEK_SET);
	fwrite(inode_bitmap, sizeof(inode_bitmap), 1, fw);
	fflush(fw);//强迫将缓冲区内的数据写回参数stream 指定的文件中
	//写入block位图
	fseek(fw, kBlockBitmapStartAddress, SEEK_SET);
	fwrite(block_bitmap, sizeof(block_bitmap), 1, fw);
	fflush(fw);//强迫将缓冲区内的数据写回参数stream 指定的文件中

	//创建根目录 "/"
	Inode cur;
	//申请inode
	int inoAddr = ialloc();
	//给这个inode申请磁盘块
	int blockAddr = balloc();
	if (error_ != 0)
		return;
	//在这个磁盘块里加入一个条目 "."
	DirItem dirlist[32] = { 0 };
	strcpy(dirlist[0].itemName, ".");
	dirlist[0].inode_address = inoAddr;

	//写回磁盘块
	fseek(fw, blockAddr, SEEK_SET);
	fwrite(dirlist, sizeof(dirlist), 1, fw);

	//给inode赋值
	cur.i_number = 0;
	cur.i_create_time = time(NULL);
	cur.i_last_change_time = time(NULL);
	cur.i_last_open_time = time(NULL);
	cur.i_file_num = 1;	//一个项，当前目录,"."根目录
	cur.i_direct_block[0] = blockAddr;
	for (i = 1; i < 10; i++) 
	{
		cur.i_direct_block[i] = 0;
	}
	cur.i_size = super_block_->s_block_size;
	cur.i_indirect_block_1 = 0;	//没使用一级间接块
	cur.i_indirect_block_2 = 0;
	cur.i_indirect_block_3 = 0;
	cur.i_type = TYPE_DIR;

	//写回inode
	fseek(fw, inoAddr, SEEK_SET);
	fwrite(&cur, sizeof(Inode), 1, fw);
	fflush(fw);
	//设置地址

	//创建回收站
	Mkdir(root_directory_address_, "Recycle");
	return;
}

int FileSystem::ialloc()	//分配i节点区函数，返回inode地址
{
	//在inode位图中顺序查找空闲的inode，找到则返回inode地址。函数结束。
	if (super_block_->s_free_inode_num == 0)
	{
		printf("没有空闲inode可以分配\n");
		error_ = 3;
		return -1;
	}
	else
	{
		//顺序查找空闲的inode
		int i;
		for (i = 0; i < super_block_->s_inode_num; i++)
		{
			if (inode_bitmap[i] == 0)	//找到空闲inode
				break;
		}
		//更新超级块
		super_block_->s_free_inode_num--;	//空闲inode数-1
		fseek(fw, kSuperBlockStartAddress, SEEK_SET);
		fwrite(super_block_, sizeof(SuperBlock), 1, fw);

		//更新inode位图
		inode_bitmap[i] = 1;
		fseek(fw, kInodeBitmapStartAddress + i, SEEK_SET);
		fwrite(&inode_bitmap[i], sizeof(bool), 1, fw);
		fflush(fw);

		return kInodeStartAddress + i * super_block_->s_inode_size;
	}
}

int FileSystem::balloc()	//磁盘块分配函数
{
	//使用超级块中的空闲块堆栈
	//计算当前栈顶
	int top;	//栈顶指针
	if (super_block_->s_free_block_num == 0)
	{	//剩余空闲块数为0
		error_ = 4;
		return -1;	//没有可分配的空闲块，返回-1
	}
	else 
	{	//还有剩余块
		top = (super_block_->s_free_block_num - 1) % super_block_->s_blocks_per_group;//如果堆栈中只剩一个，top就为0
	}
	//将栈顶取出
	//如果已是栈底，将当前块号地址返回，即为栈底块号，并将栈底指向的新空闲块堆栈覆盖原来的栈
	int retAddr;

	if (top == 0)
	{
		retAddr = super_block_->s_free_addr;
		super_block_->s_free_addr = super_block_->s_free[0];	//取出下一个存有空闲块堆栈的空闲块的位置，更新空闲块堆栈指针

		//取出对应空闲块内容，覆盖原来的空闲块堆栈
		//取出下一个空闲块堆栈，覆盖原来的
		fseek(fr, super_block_->s_free_addr, SEEK_SET);
		fread(super_block_->s_free, sizeof(super_block_->s_free), 1, fr);
		fflush(fr);
		super_block_->s_free_block_num--;
	}
	else
	{	//如果不为栈底，则将栈顶指向的地址返回，栈顶指针-1.
		retAddr = super_block_->s_free[top];	//保存返回地址
		super_block_->s_free[top] = -1;	//清栈顶
		top--;		//栈顶指针-1
		super_block_->s_free_block_num--;	//空闲块数-1

	}

	//更新超级块
	fseek(fw, kSuperBlockStartAddress, SEEK_SET);
	fwrite(super_block_, sizeof(SuperBlock), 1, fw);
	fflush(fw);

	//更新block位图
	block_bitmap[(retAddr - kBlockStartAddress) / BLOCK_SIZE] = 1;
	fseek(fw, (retAddr - kBlockStartAddress) / BLOCK_SIZE + kBlockBitmapStartAddress, SEEK_SET);	//(retAddr-Block_StartAddr)/BLOCK_SIZE为第几个空闲块
	fwrite(&block_bitmap[(retAddr - kBlockStartAddress) / BLOCK_SIZE], sizeof(bool), 1, fw);
	fflush(fw);

	return retAddr;
}

void FileSystem::Mkdir(int previous_file, const char *name)	//目录创建函数。参数：上一层目录文件inode地址 ,要创建的目录名
{
	int chiinoAddr = ialloc();	//分配当前节点地址 
	//设置新条目的inode
	Inode p;
	p.i_number = (chiinoAddr - kInodeStartAddress) / super_block_->s_inode_size;
	p.i_create_time = time(NULL);
	p.i_last_change_time = time(NULL);
	p.i_last_open_time = time(NULL);
	p.i_file_num = 2;	//两个项，当前目录,"."和".."
	//分配这个inode的磁盘块，在磁盘号中写入两条记录 . 和 ..
	int nextblockAddr = balloc();
	if (error_ != 0)
	{
		return;
	}
	DirItem dirlist2[32] = { 0 };	//临时目录项列表 - 2
	strcpy(dirlist2[0].itemName, ".");
	strcpy(dirlist2[1].itemName, "..");
	dirlist2[0].inode_address = chiinoAddr;	//当前目录inode地址
	dirlist2[1].inode_address = previous_file;	//父目录inode地址

	//写入到当前目录的磁盘块
	fseek(fw, nextblockAddr, SEEK_SET);
	fwrite(dirlist2, sizeof(dirlist2), 1, fw);

	p.i_direct_block[0] = nextblockAddr;
	int k;
	for (k = 1; k < 10; k++)
	{
		p.i_direct_block[k] = 0;
	}
	p.i_size = super_block_->s_block_size;
	p.i_indirect_block_1 = 0;	//没使用一级间接块
	p.i_indirect_block_2 = 0;
	p.i_indirect_block_3 = 0;
	p.i_type = TYPE_DIR;

	//将inode写入到申请的inode地址
	fseek(fw, chiinoAddr, SEEK_SET);
	fwrite(&p, sizeof(Inode), 1, fw);
	AddFileToFolder(previous_file, chiinoAddr, name, p.i_type);
	if (error_ != 0)
	{
		bfree(nextblockAddr);
		ifree(chiinoAddr);
	}
/*	if (strlen(name) >= MAX_NAME_SIZE)
	{
		error_ = 6;
		return;
	}
	int i;
	DirItem dirlist[32];	//临时目录清单
	Inode cur;
	fseek(fr, previous_file, SEEK_SET);
	fread(&cur, sizeof(Inode), 1, fr);
	if (cur.i_file_num == 320)
	{
		error_ = 7;
		return;
	}

	int posi = -1, posj = -1;
	DirectoryLookup(previous_file, name, TYPE_DIR, posi, posj);
	if (posi != -1 && posj != -1)//查找到同名文件
	{
		error_ = 8;
		return;
	}
	DirectoryLookup(previous_file, "", TYPE_DIR, posi, posj);

	if (posi != -1&&posj!=-1)
	{
		//找到这个空闲位置
		//取出这个直接块，要加入的目录条目的位置
		fseek(fr, cur.i_direct_Block[posi], SEEK_SET);
		fread(dirlist, sizeof(dirlist), 1, fr);
		fflush(fr);
		//创建这个目录项
		strcpy(dirlist[posj].itemName, name);	//目录名
		//写入两条记录 "." ".."，分别指向当前inode节点地址，和父inode节点
		int chiinoAddr = ialloc();	//分配当前节点地址 
		if (error_!=0)
			return;
		dirlist[posj].inode_address = chiinoAddr; //给这个新的目录分配的inode地址
		//设置新条目的inode
		Inode p;
		p.i_number = (chiinoAddr - kInodeStartAddress) / super_block_->s_inode_size;
		p.i_create_time = time(NULL);
		p.i_last_change_time = time(NULL);
		p.i_last_open_time = time(NULL);
		p.i_file_num = 2;	//两个项，当前目录,"."和".."
		//分配这个inode的磁盘块，在磁盘号中写入两条记录 . 和 ..
		int nextblockAddr = balloc();
		if (error_ != 0)
		{
			return;
		}
		DirItem dirlist2[32] = { 0 };	//临时目录项列表 - 2
		strcpy(dirlist2[0].itemName, ".");
		strcpy(dirlist2[1].itemName, "..");
		dirlist2[0].inode_address = chiinoAddr;	//当前目录inode地址
		dirlist2[1].inode_address = previous_file;	//父目录inode地址

		//写入到当前目录的磁盘块
		fseek(fw, nextblockAddr, SEEK_SET);
		fwrite(dirlist2, sizeof(dirlist2), 1, fw);

		p.i_direct_Block[0] = nextblockAddr;
		int k;
		for (k = 1; k < 10; k++)
		{
			p.i_direct_Block[k] = 0;
		}
		p.i_size = super_block_->s_block_size;
		p.i_indirect_block_1 = 0;	//没使用一级间接块
		p.i_indirect_block_2 = 0;
		p.i_indirect_block_3 = 0;
		p.i_type = TYPE_DIR;

		//将inode写入到申请的inode地址
		fseek(fw, chiinoAddr, SEEK_SET);
		fwrite(&p, sizeof(Inode), 1, fw);

		//将当前目录的磁盘块写回
		fseek(fw, cur.i_direct_Block[posi], SEEK_SET);
		fwrite(dirlist, sizeof(dirlist), 1, fw);

		//写回inode
		cur.i_file_num++;
		fseek(fw, previous_file, SEEK_SET);
		fwrite(&cur, sizeof(Inode), 1, fw);
		fflush(fw);
		return;
	}


	for (i = 0; i < 10; i++)
		if (cur.i_direct_Block[i] == 0)
			break;
	if (i != 10)
	{
		if (error_ == 5)
			error_ = 0;
		int current_block_adress = balloc();
		if (error_ != 0)
			return;
		cur.i_direct_Block[i] = current_block_adress;
		DirItem dirlist3[32];

		int nextinoAddr = ialloc();	//分配当前节点地址 
		if (error_ != 0)
			return;
		dirlist3[0].inode_address = nextinoAddr;
		strcpy(dirlist3[0].itemName, name);

		Inode p2;
		p2.i_number = (nextinoAddr - kInodeStartAddress) / super_block_->s_inode_size;
		p2.i_create_time = time(NULL);
		p2.i_last_change_time = time(NULL);
		p2.i_last_open_time = time(NULL);
		p2.i_file_num = 2;	//两个项，当前目录,"."和".."

		//分配这个inode的磁盘块，在磁盘号中写入两条记录 . 和 ..
		int nextblockAddr = balloc();
		p2.i_direct_Block[0] = nextblockAddr;
		if (error_ != 0)
		{
			return;
		}
		DirItem dirlist4[32] = { 0 };	//临时目录项列表 - 2
		strcpy(dirlist4[0].itemName, ".");
		strcpy(dirlist4[1].itemName, "..");
		dirlist4[0].inode_address = nextinoAddr;	//当前目录inode地址
		dirlist4[1].inode_address = previous_file;	//父目录inode地址

		//写入到当前目录的磁盘块
		fseek(fw, nextblockAddr, SEEK_SET);
		fwrite(dirlist4, sizeof(dirlist4), 1, fw);

		int k;
		for (k = 1; k < 10; k++)
		{
			p2.i_direct_Block[k] = 0;
		}
		p2.i_size = super_block_->s_block_size;
		p2.i_indirect_block_1 = 0;	//没使用一级间接块
		p2.i_indirect_block_2 = 0;
		p2.i_indirect_block_3 = 0;
		p2.i_type = TYPE_DIR;

		//将inode写入到申请的inode地址
		fseek(fw, nextinoAddr, SEEK_SET);
		fwrite(&p2, sizeof(Inode), 1, fw);

		//将当前目录的磁盘块写回
		fseek(fw, cur.i_direct_Block[i], SEEK_SET);
		fwrite(dirlist3, sizeof(dirlist3), 1, fw);

		//写回inode
		cur.i_file_num++;
		fseek(fw, previous_file, SEEK_SET);
		fwrite(&cur, sizeof(Inode), 1, fw);
		fflush(fw);
		return;
	}*/
}

int FileSystem::DirectoryLookup(int previous_file, const char *name, bool type, int &posi, int &posj)
{
	DirItem dirlist[32];	//临时目录清单
	//从这个地址取出inode
	Inode cur;
	fseek(fr, previous_file, SEEK_SET);
	fread(&cur, sizeof(Inode), 1, fr);

	int i = 0;
	int cnt = cur.i_file_num ;	//目录项数
	posi = -1, posj = -1;
	while (i < 320) 
	{
		//320个目录项之内，可以直接在直接块里找
		int dno = i / 32;	//在第几个直接块里,一共十个直接块

		if (cur.i_direct_block[dno] == -1)
		{
			//没有使用直接块
			i += 32;
			continue;
		}
		//取出这个直接块，要加入的目录条目的位置
		fseek(fr, cur.i_direct_block[dno], SEEK_SET);
		fread(dirlist, sizeof(dirlist), 1, fr);
		fflush(fr);

		//输出该磁盘块中的所有目录项
		int j;
		for (j = 0; j < 32; j++) 
		{
			if (strcmp(dirlist[j].itemName, name) == 0) 
			{
				if (dirlist[j].inode_address == 0) // 找到一个空闲记录，将新目录创建到这个位置,记录这个位置
				{
					if (posi == -1)
					{
						posi = dno;
						posj = j;
					}
					return 0;
				}
				else
				{
					Inode tmp;
					fseek(fr, dirlist[j].inode_address, SEEK_SET);
					fread(&tmp, sizeof(Inode), 1, fr);
					if (tmp.i_type == type)
					{
						if (posi == -1)
						{
							posi = dno;
							posj = j;
						}
						return  dirlist[j].inode_address;
					}
				}
			}
			i++;
		}
	}
	error_ = 5;
	return 0;
}

void FileSystem::Open()	//安装文件系统，将虚拟磁盘文件中的关键信息如超级块读入到内存
{
	root_directory_address_ = kInodeStartAddress;			//第一个inode地址
	current_directory_address_ = root_directory_address_;
	current_directory_path_ = "/";

	//读写虚拟磁盘文件，读取超级块，读取inode位图，block位图，读取主目录，读取etc目录，读取管理员admin目录，读取用户xiao目录，读取用户passwd文件。
	//读取超级块
	fseek(fr, kSuperBlockStartAddress, SEEK_SET);//转移指针
	fread(super_block_, sizeof(SuperBlock), 1, fr);//读取指定内容
	//读取inode位图
	fseek(fr, kInodeBitmapStartAddress, SEEK_SET);
	fread(inode_bitmap, sizeof(inode_bitmap), 1, fr);
	//读取block位图
	fseek(fr, kBlockBitmapStartAddress, SEEK_SET);
	fread(block_bitmap, sizeof(block_bitmap), 1, fr);
	return;

}

void FileSystem::Parser()
{
	//
	//char inputcmd[100];
	//char absolutePath[100];
	//char *argv[64];
	//int count = 0, argc = 0;
	//printf("> "); // 每次命令前的标志, 类似于“$ ~”
	//fgets(inputcmd, sizeof(inputcmd), stdin);
	//// ****************** close 命令 ******************
	//if (strcmp(inputcmd, "close\n") == 0) 
	//{
	//	return;//关闭
	//}
	////**********************命令解读*******************
	//count = strlen(inputcmd) - 1;
	//for (int i = 0; i <= count; i++) // 用'\0'替换' ','\n'，目的是要把指令和所有参数都变成字符串
	//{ 
	//	if (inputcmd[i] == ' ' || inputcmd[i] == '\n')
	//		inputcmd[i] = '\0';
	//}
	//argv[argc] = &inputcmd[0];
	//argc++;
	//for (int i = 1; i <= count - 1; i++)  // 把每个字符串的位址存到argv
	//{
	//	if (inputcmd[i] == '\0' && inputcmd[i + 1] != '\0') 
	//	{
	//		argv[argc] = &inputcmd[i + 1];
	//		argc++;
	//	}
	//}
	//
	///**********************************
	//	 **********************************
	//	 **********************************
	//	 ********   Command APIs   ********
	//	 **********************************
	//	 **********************************
	//	 **********************************/

	//	 // ****************** att 命令 *****************
	//if (strcmp(argv[0], "att") == 0) 
	//{
	//	// att 命令, 待填入
	//}
	//// ****************** dr 命令 , 相当于 ls 命令 *****************
	//		// ****************** dr 命令 , 相当于 ls 命令 *****************
	//else if (strcmp(argv[0], "dr") == 0) 
	//{
	//	//strcpy(tempReturn, ".");
	//	//strcat(tempReturn, ":\n");
	//	//dir_ls(ls_return, PATH);
	//	//strcat(tempReturn, ls_return);

	//	if (argc == 1)
	//	{
	//		printf("%s", tempReturn);
	//	}
	//	else if (argc >= 2)
	//	{
	//		for (int i = 1; i <= argc - 1; i++)
	//		{
	//			if (strcmp(argv[i], ">") == 0 && (i + 1) <= argc - 1)
	//			{
	//				relative_to_absolute_path(argv[i + 1], absolutePath);
	//				printf("[write_file_by_path]path:%s buf=%s size=%lu\n", absolutePath, tempReturn, strlen(tempReturn));

	//				if (write_file_by_path(absolutePath, tempReturn, strlen(tempReturn)) >= 0)
	//				{
	//					//if(1){
	//					printf("%s finish\n", tempReturn);
	//					break;
	//				}
	//				else
	//				{
	//					printf("write file error.\n");
	//					break;
	//				}

	//			}
	//			else if (strcmp(argv[i], ">") == 0 && i == argc - 1)
	//			{
	//				printf("write file error\n");
	//				break;
	//			}
	//			else if (strcmp(argv[i], ">") != 0 && i == 1)
	//			{
	//				relative_to_absolute_path(argv[i], absolutePath);
	//				if (dir_change(absolutePath) < 0)
	//				{
	//					//if(0){
	//					strcpy(tempReturn, argv[i]);
	//					strcat(tempReturn, ":\n");
	//					strcat(tempReturn, "No such file or directory.\n");
	//				}
	//				else
	//				{
	//					strcpy(tempReturn, argv[i]);
	//					strcat(tempReturn, ":\n");
	//					dir_ls(ls_return, absolutePath);
	//					strcat(tempReturn, ls_return);
	//					//strcat(tempReturn,"dir1\ndate.txt\ncat.mp3\n");
	//				}
	//			}
	//			else if (strcmp(argv[i], ">") != 0) {
	//				relative_to_absolute_path(argv[i], absolutePath);
	//				if (dir_change(absolutePath) < 0) {
	//					//if(0){
	//					strcat(tempReturn, argv[i]);
	//					strcat(tempReturn, ":\n");
	//					strcat(tempReturn, "No such file or directory.\n");
	//				}
	//				else {
	//					strcat(tempReturn, argv[i]);
	//					strcat(tempReturn, ":\n");
	//					dir_ls(ls_return, absolutePath);
	//					strcat(tempReturn, ls_return);
	//					//strcat(tempReturn,"dir1\ndate.txt\ncat.mp3\n");
	//				}
	//			}
	//			if (i == argc - 1)
	//			{
	//				if (strcmp(tempReturn, "") != 0)
	//					printf("%s", tempReturn);
	//			}
	//		}
	//	}
	//}
}

void FileSystem::Cd(int previous_file, const char *path)
{
	std::string cd_path = path;
	if (cd_path[0] == '/')
	{
		int len = cd_path.find('/', 1), posi = -1, posj = -1,address=0;
		if (len != std::string::npos)
		{
			address=DirectoryLookup(root_directory_address_, cd_path.substr(1, len-1).c_str(), TYPE_DIR, posi, posj);
			if (error_!=0)
			{
				return;
			}
			Cd(address, cd_path.substr(cd_path.find('/', 1) + 1).c_str());
		}
		else
		{
			address=DirectoryLookup(root_directory_address_, cd_path.substr(1).c_str(), TYPE_DIR, posi, posj);
			if (error_ != 0)
			{
				return;
			}
			current_directory_path_ = "/" + cd_path.substr(1);
			current_directory_address_ = address;
		}
	}
	else if (cd_path[0] == '.')
	{
		if (cd_path.length() == 1)
		{
			return;
		}
		else if (cd_path[0] == '.')
		{
			Inode cur;
			fseek(fr, previous_file, SEEK_SET);
			fread(&cur, sizeof(Inode), 1, fr);
			DirItem dirlist[32];	//临时目录清单
			fseek(fr, cur.i_direct_block[0], SEEK_SET);
			fread(dirlist, sizeof(dirlist), 1, fr);
			current_directory_address_ = dirlist[1].inode_address;
			current_directory_path_ = current_directory_path_.substr(1, current_directory_path_.find_last_of('/') - 1);
			return;
		}
	}
	else
	{
		int len = cd_path.find('/', 1), posi = -1, posj = -1, address = 0;
		if (len != std::string::npos)
		{
			address = DirectoryLookup(previous_file, cd_path.substr(1, len - 1).c_str(), TYPE_DIR, posi, posj);
			if (error_ != 0)
			{
				return;
			}
			Cd(address, cd_path.substr(cd_path.find('/', 1) + 1).c_str());
		}
		else
		{
			address = DirectoryLookup(previous_file, cd_path.substr(1).c_str(), TYPE_DIR, posi, posj);
			if (error_ != 0)
			{
				return;
			}
			current_directory_path_ = current_directory_path_+ "/" + cd_path.substr(1);
			current_directory_address_ = address;
			return;
		}
	}
}

void FileSystem::Move(int be_shared_folder,const char *be_shared_name, int shar_to_folder,int file_type)
{
	Inode be_shared_folder_inode;
	DirItem dirlist[32];	//临时目录清单
	fseek(fr, be_shared_folder, SEEK_SET);
	fread(&be_shared_folder_inode, sizeof(Inode), 1, fr);

	int posi = -1, posj = -1;
	DirectoryLookup(be_shared_folder, be_shared_name, file_type, posi, posj);
	if (error_!=0)
		return;
	fseek(fr, be_shared_folder_inode.i_direct_block[posi], SEEK_SET);
	fread(dirlist, sizeof(dirlist), 1, fr);

	Inode file;
	fseek(fr, dirlist[posj].inode_address, SEEK_SET);
	fread(&file, sizeof(Inode), 1, fr);
	AddFileToFolder(shar_to_folder, dirlist[posj].inode_address, dirlist[posj].itemName, file.i_type);
	if (error_ != 0)
		return;
	if (file.i_type == TYPE_DIR)
	{
		DirItem dirlist2[32];	//临时目录清单
		fseek(fr, file.i_direct_block[0], SEEK_SET);
		fread(dirlist2, sizeof(dirlist2), 1, fr);
		fflush(fr);
		dirlist2[1].inode_address = shar_to_folder;
		fseek(fw, file.i_direct_block[0], SEEK_SET);
		fwrite(dirlist2, sizeof(dirlist2), 1, fw);
		fflush(fw);
	}

	dirlist[posj].inode_address = 0;
	memset(dirlist[posj].itemName, 0, sizeof(dirlist[posj].itemName));
	fseek(fw, be_shared_folder_inode.i_direct_block[posi], SEEK_SET);
	fwrite(dirlist, sizeof(dirlist), 1, fw);
	be_shared_folder_inode.i_file_num--;
	fseek(fw, be_shared_folder, SEEK_SET);
	fwrite(&be_shared_folder_inode, sizeof(Inode), 1, fw);
	fflush(fw);
}

void FileSystem::AddFileToFolder(int folder_inode_adress, int file_inode_adress,const char* file_name,bool file_type)
{

	DirItem dirlist[32];	//临时目录清单
	Inode folder_inode;
	int i;
	fseek(fr, folder_inode_adress, SEEK_SET);
	fread(&folder_inode, sizeof(Inode), 1, fr);
	if (folder_inode.i_file_num == 320)
	{
		error_ = 7;
		return;
	}
	if (folder_inode.i_type != TYPE_DIR)
	{
		error_ = 9;
		return;
	}
	int posi = -1, posj = -1;
	DirectoryLookup(folder_inode_adress, file_name, file_type, posi, posj);
	if (posi != -1 && posj != -1)//查找到同名文件
	{
		error_ = 8;
		return;
	}
	DirectoryLookup(folder_inode_adress, "", file_type, posi, posj);
	if (posi != -1 && posj != -1)		//找到这个空闲位置，取出这个直接块，要加入的目录条目的位置
	{
		fseek(fr, folder_inode.i_direct_block[posi], SEEK_SET);
		fread(dirlist, sizeof(dirlist), 1, fr);
		fflush(fr);
		//创建这个目录项
		strcpy(dirlist[posj].itemName, file_name);	//目录名
		dirlist[posj].inode_address = file_inode_adress;

		//将当前目录的磁盘块写回
		fseek(fw, folder_inode.i_direct_block[posi], SEEK_SET);
		fwrite(dirlist, sizeof(dirlist), 1, fw);

		//写回inode
		folder_inode.i_file_num++;
		fseek(fw, folder_inode_adress, SEEK_SET);
		fwrite(&folder_inode, sizeof(Inode), 1, fw);
		fflush(fw);
		return;
	}
	
	for (i = 0; i < 10; i++)
		if (folder_inode.i_direct_block[i] == 0)
			break;
	if (i != 10)
	{
		if (error_ == 5)
			error_ = 0;
		int current_block_adress = balloc();
		if (error_ != 0)
			return;
		folder_inode.i_direct_block[i] = current_block_adress;
		DirItem dirlist2[32];

		dirlist2[0].inode_address = file_inode_adress;
		strcpy(dirlist2[0].itemName, file_name);

		//将当前目录的磁盘块写回
		fseek(fw, folder_inode.i_direct_block[i], SEEK_SET);
		fwrite(dirlist2, sizeof(dirlist2), 1, fw);

		//写回inode
		folder_inode.i_file_num++;
		fseek(fw, folder_inode_adress, SEEK_SET);
		fwrite(&folder_inode, sizeof(Inode), 1, fw);
		fflush(fw);
		return;
	}
}

void FileSystem::bfree(int addr)	//磁盘块释放函数
{
	//判断
	//该地址不是磁盘块的起始地址
	if ((addr - kBlockStartAddress) % super_block_->s_block_size != 0)
	{
		error_ = 10;
		return;
	}
	unsigned int bno = (addr - kBlockStartAddress) / super_block_->s_block_size;	//inode节点号
	/*//该地址还未使用，不能释放空间
	if (block_bitmap[bno] == 0) 
	{
		printf("该block（磁盘块）还未使用，无法释放\n");
		return false;
	}*/

	//可以释放
	//计算当前栈顶
	int top;	//栈顶指针
	if (super_block_->s_free_block_num == super_block_->s_block_num) //没有非空闲的磁盘块
	{	
		error_ = 11;
		return;	//没有可分配的空闲块，返回-1
	}
	else 
	{	//非满
		top = (super_block_->s_free_block_num - 1) % super_block_->s_blocks_per_group;

		//清空block内容
		char tmp[BLOCK_SIZE] = { 0 };
		fseek(fw, addr, SEEK_SET);
		fwrite(tmp, sizeof(tmp), 1, fw);

		if (top == super_block_->s_blocks_per_group - 1)//该栈已满
		{	
			//该空闲块作为新的空闲块堆栈
			super_block_->s_free[0] = super_block_->s_free_addr;	//新的空闲块堆栈第一个地址指向旧的空闲块堆栈指针
			int i;
			for (i = 1; i < super_block_->s_blocks_per_group; i++) {
				super_block_->s_free[i] = -1;	//清空栈元素的其它地址
			}
			fseek(fw, addr, SEEK_SET);
			fwrite(super_block_->s_free, sizeof(super_block_->s_free), 1, fw);	//填满这个磁盘块，512字节

		}
		else //栈还未满
		{	
			top++;	//栈顶指针+1
			super_block_->s_free[top] = addr;	//栈顶放上这个要释放的地址，作为新的空闲块
		}
	}


	//更新超级块
	super_block_->s_free_block_num++;	//空闲块数+1
	fseek(fw, kSuperBlockStartAddress, SEEK_SET);
	fwrite(super_block_, sizeof(SuperBlock), 1, fw);

	//更新block位图
	block_bitmap[bno] = 0;
	fseek(fw, bno + kBlockBitmapStartAddress, SEEK_SET);	//(addr-Block_StartAddr)/BLOCK_SIZE为第几个空闲块
	fwrite(&block_bitmap[bno], sizeof(bool), 1, fw);
	fflush(fw);

	return;
}

void FileSystem::ifree(int addr)	//释放i结点区函数
{
	//判断
	if ((addr - kInodeStartAddress) % super_block_->s_inode_size != 0) 
	{
		error_ = 12;
		return;
	}
	unsigned short ino = (addr - kInodeStartAddress) / super_block_->s_inode_size;	//inode节点号
	if (inode_bitmap[ino] == 0) 
	{
		error_ = 13;
		return;
	}

	//清空inode内容
	Inode tmp = { 0 };
	fseek(fw, addr, SEEK_SET);
	fwrite(&tmp, sizeof(tmp), 1, fw);

	//更新超级块
	super_block_->s_free_inode_num++;
	//空闲inode数+1
	fseek(fw, kSuperBlockStartAddress, SEEK_SET);
	fwrite(super_block_, sizeof(SuperBlock), 1, fw);

	//更新inode位图
	inode_bitmap[ino] = 0;
	fseek(fw, kInodeBitmapStartAddress + ino, SEEK_SET);
	fwrite(&inode_bitmap[ino], sizeof(bool), 1, fw);
	fflush(fw);

	return;
}

void FileSystem::PutInRecycle(int previous_file, const char *file_name,bool file_type)
{
	int posi = -1, posj = -1;
	int file_address = DirectoryLookup(previous_file, file_name, file_type, posi, posj);
	if (error_ != 0)
		return;
	Inode file;
	fseek(fr, file_address, SEEK_SET);
	fread(&file, sizeof(Inode),1, fr);
	fflush(fr);
	file.i_pre_folder_adress = previous_file;
	AddFileToFolder(recycle_directory_address_, file_address, file_name, file.i_type);
	fseek(fw, file_address, SEEK_SET);
	fwrite(&file, sizeof(Inode), 1, fw);
	fflush(fw);
	return;
}

void FileSystem::RestoreFromRecycle(const char *file_name, bool file_type)
{
	int posi = -1, posj = -1;
	int file_address = DirectoryLookup(recycle_directory_address_, file_name, file_type, posi, posj);
	if (error_ != 0)
		return;
	Inode file;
	fseek(fr, file_address, SEEK_SET);
	fread(&file, sizeof(Inode), 1, fr);
	AddFileToFolder(recycle_directory_address_, file_address, file_name, file.i_type);
	file.i_pre_folder_adress = 0;
	return;
}

void FileSystem::EmptyRecycle(int inode_dress)	//清空回收站
{}

void FileSystem::win_cp_minfs(int parinoAddr, const char* name, const char* win_path) //win向minifs拷贝命令
{

	int chiinoAddr = ialloc();	//分配当前节点地址 
	//设置新条目的inode
	Inode p;
	p.i_number = (chiinoAddr - kInodeStartAddress) / super_block_->s_inode_size;
	p.i_create_time = time(NULL);
	p.i_last_change_time = time(NULL);
	p.i_last_open_time = time(NULL);
	p.i_file_num = 1;//硬链接

	int len = 0;  //文件大小
	FILE *fp = fopen(win_path, "rb");
	if (fp == NULL)
	{
		error_ = 14;
		return;
	}
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	if (len > super_block_->s_free_block_num*BLOCK_SIZE);
	{
		error_ = 15;
		return;
	}
	p.i_size = len; //获取文件大小
	fclose(fp);
	if (len % 1024 != 0)
		len = ((len / 1024) + 1) * 1024;
	char *file_content = new char[len];
	fp = fopen(win_path, "rb");
	fread(file_content, len, 1, fp);   //file_content 用来存文件内容 len表示文件长度
	int storage = 0;   //已储存大小
	
	AddFileToFolder(parinoAddr, chiinoAddr, name, TYPE_FILE);
	if (error_==8)
	{
		error_ = 0;
		printf("文件已存在\n");
		printf("覆盖/重命名(y/n)\n");
		char ju;
		scanf("%c", &ju);
		if (ju == 'y')
		{
			//删除原inode
		}
		else if (ju == 'n')
		{
			printf("请输入新的文件名:");
			char *a = new char[80];
			scanf("%s", a);
			win_cp_minfs(parinoAddr, a, win_path);
			delete a;
			return;
		}
	}

	if (error_ != 0)
		return;
	for (int i = 0; i < 10; i++)   //分配直接块
	{
		p.i_direct_block[i] = balloc();
		if (error_ != 0)
			return;
		fseek(fw, p.i_direct_block[i], SEEK_SET);
		fwrite(file_content + storage, 1024, 1, fw);
		storage += 1024;
		if (storage >= len)
			break;
	}

	if (storage >= len)
	{
		delete file_content;
		return;
	}

	p.i_indirect_block_1 = balloc();    //分配一级间接块
	if (error_ != 0)
		return;
	int *tmp = new int[256];
	for (int i = 0; i < 256; i++)
	{
		tmp[i] = balloc();
		if (error_ != 0)
			return;
		fseek(fw, tmp[i], SEEK_SET);
		fwrite(file_content + storage, 1024, 1, fw);
		storage += 1024;
		if (storage >= len)
			break;
	}
	fseek(fw, p.i_indirect_block_1, SEEK_SET);
	fwrite(tmp, 1024, 1, fw);
	delete tmp;

	if (storage >= len)
	{
		delete file_content;
		return;
	}

	p.i_indirect_block_2 = balloc(); //分配二级间接块
	if (error_ != 0)
		return;
	tmp = new int[256];
	for (int i = 0; i < 256; i++)
	{
		int* tmp_1 = new int[256];
		for (int j = 0; j < 256; j++)
		{
			tmp_1[j] = balloc();
			if (error_ != 0)
				return;
			fseek(fw, tmp_1[j], SEEK_SET);
			fwrite(file_content + storage, 1024, 1, fw);
			storage += 1024;
			if (storage >= len)
				break;
		}
		fseek(fw, tmp[i], SEEK_SET);
		fwrite(tmp_1, 1024, 1, fw);
		delete tmp_1;
		if (storage >= len)
			break;
	}
	fseek(fw, p.i_indirect_block_2, SEEK_SET);
	fwrite(tmp, 1024, 1, fw);
	delete tmp;

	if (storage >= len)
	{
		delete file_content;
		return;
	}

	p.i_indirect_block_3 = balloc();//分配三级间接块
	if (error_ != 0)
		return;
	tmp = new int[256];
	for (int i = 0; i < 256; i++)
	{
		tmp[i] = balloc();
		if (error_ != 0)
			return;
		int* tmp_1 = new int[256];
		for (int j = 0; j < 256; j++)
		{
			tmp_1[j] = balloc();
			if (error_ != 0)
				return;
			int* tmp_2 = new int[256];
			for (int p = 0; p < 256; p++)
			{
				tmp_2[p] = balloc();
				if (error_ != 0)
					return;
				fseek(fw, tmp_2[p], SEEK_SET);
				fwrite(file_content + storage, 1024, 1, fw);
				storage += 1024;
				if (storage >= len)
					break;
			}
			fseek(fw, tmp_1[j], SEEK_SET);
			fwrite(tmp_2, 1024, 1, fw);
			delete tmp_2;
			if (storage >= len)
				break;
		}
		fseek(fw, tmp[i], SEEK_SET);
		fwrite(tmp_1, 1024, 1, fw);
		delete tmp_1;
		if (storage >= len)
			break;
	}
	fseek(fw, p.i_indirect_block_3, SEEK_SET);
	fwrite(tmp, 1024, 1, fw);
	delete tmp;

	delete file_content;

	p.i_type = TYPE_FILE;
	fseek(fw, chiinoAddr, SEEK_SET);
	fwrite(&p, sizeof(Inode), 1, fw);
	return;

}

void FileSystem::minifs_cp_win(int parinoAddr, const char* name, const char* win_path)//minifs向win拷贝
{
//	DirItem dirlist[32]; //临时目录清单
	Inode cur;
	fseek(fr, parinoAddr, SEEK_SET);
	fread(&cur, sizeof(Inode), 1, fr); //取出inode
	int len = cur.i_size;
	if (cur.i_size % 1024 != 0)
	{
		len = ((cur.i_size / 1024) + 1) * 1024;
	}
	char *file_content = new char[len];  //创建空间
	char *mov_ptr = file_content;      //移动指针
	for (int i = 0; i < 10; i++)    //读取直接块
	{
		if (cur.i_direct_block[i] == 0)
			break;
		fseek(fr, cur.i_direct_block[i], SEEK_SET);
		fread(mov_ptr, 1024, 1, fr);
		mov_ptr += 1024;
	}

	if (cur.i_indirect_block_1 != 0)   //读取一级间接块
	{
		int* tmp = new int[256];
		fseek(fr, cur.i_indirect_block_1, SEEK_SET);
		fread(tmp, 1024, 1, fr);
		for (int i = 0; i < 256; i++)
		{
			if (tmp[i] == 0)
				break;
			fseek(fr, tmp[i], SEEK_SET);
			fread(mov_ptr, 1024, 1, fr);
			mov_ptr += 1024;
		}
		delete tmp;
	}
	if (cur.i_indirect_block_2 != 0)  //读取二级间接块
	{
		int* tmp = new int[256];
		fseek(fr, cur.i_indirect_block_2, SEEK_SET);
		fread(tmp, 1024, 1, fr);
		for (int i = 0; i < 256; i++)
		{
			if (tmp[i] == 0)
				break;
			int *tmp_1 = new int[256];
			fseek(fr, tmp[i], SEEK_SET);
			fread(tmp_1, 1024, 1, fr);
			for (int j = 0; j < 256; j++)
			{
				if (tmp_1[j] == 0)
					break;
				fseek(fr, tmp_1[j], SEEK_SET);
				fread(mov_ptr, 1024, 1, fr);
				mov_ptr += 1024;
			}
			delete tmp_1;
		}
		delete tmp;
	}
	if (cur.i_indirect_block_3 != 0)  //读取三级间接块
	{
		int* tmp = new int[256];
		fseek(fr, cur.i_indirect_block_3, SEEK_SET);
		fread(tmp, 1024, 1, fr);
		for (int i = 0; i < 256; i++)
		{
			if (tmp[i] == 0)
				break;
			int* tmp_1 = new int[256];
			fseek(fr, tmp[i], SEEK_SET);
			fread(tmp_1, 1024, 1, fr);
			for (int j = 0; j < 256; j++)
			{
				if (tmp_1[j] == 0)
					break;
				int* tmp_2 = new int[256];
				fseek(fr, tmp_1[j], SEEK_SET);
				fread(tmp_2, 1024, 1, fr);
				for (int p = 0; p < 256; p++)
				{
					if (tmp_2[p] == 0)
						break;
					fseek(fr, tmp_2[p], SEEK_SET);
					fread(mov_ptr, 1024, 1, fr);
					mov_ptr += 1024;
				}
				delete tmp_2;
			}
			delete tmp_1;
		}
		delete tmp;
	}
	FILE* win_file = fopen(win_path, "wb");
	fwrite(file_content, len, 1, win_file);
	delete file_content;
	return;
}

/*bool minifs_cp_minifs(int parinoAddr, int parinoAddr2, const char name[])//minifs向minifs拷贝  原 目标 目标
{
	if (strlen(name) >= MAX_NAME_SIZE)
	{
		printf("超过最大文件名长度\n");
		return false;
	}
	DirItem dirlist[32]; //临时目录清单
	Inode cur_2;
	fseek(fr, parinoAddr2, SEEK_SET);
	fread(&cur_2, sizeof(Inode), 1, fr); //取出inode
	int i = 0;
	int posi = -1, posj = -1;
	int dno;
	int cnt = cur_2.i_cnt + 1;
	while (i < 320)   //320个目录项
	{
		dno = i / 32; //第几个直接块里
		if (cur_2.i_dirBlock[dno] == -1)
		{
			i += 32;
			continue;
		}
	}
	fseek(fr, cur_2.i_dirBlock[dno], SEEK_SET);
	fread(dirlist, sizeof(dirlist), 1, fr);
	fflush(fr);
	int j;
	for (j = 0; j < 16; j++)  //查找该块所有目录项
	{
		if (posi == -1 && strcmp(dirlist[j].itemName, "") == 0) //找空闲记录，将文件创建到这个位置
		{
			posi = dno;
			posj = j;
		}
		else if (strcmp(dirlist[j].itemName, name) == 0)  //重名，判断是否为文件
		{
			Inode cur2;
			fseek(fr, dirlist[j].inodeAddr, SEEK_SET);
			fread(&cur2, sizeof(Inode), 1, fr);
			if (((cur2.i_mode >> 9) & 1) == 0)    //重名且为文件，不能创建
			{
				printf("文件已存在\n");
				printf("覆盖/重命名(y/n)\n");
				char ju;
				scanf("%c", &ju);
				if (ju == 'y')
				{
					posi = dno;
					posj = j;
					break;
				}
				else if (ju == 'n')
				{
					printf("请输入新的文件名:");
					char *a = new char[80];
					scanf("%s", a);
					if (minifs_cp_minifs(parinoAddr, parinoAddr2, a))
					{
						delete a;
						return true;
					}
				}
				return false;
			}
		}
		i++;
	}
	if (posi != -1)   //找到目录项
	{
		fseek(fr, cur_2.i_dirBlock[posi], SEEK_SET);
		fread(dirlist, sizeof(dirlist), 1, fr);       //取出磁盘块
		fflush(fr);
		strcpy(dirlist[posj].itemName, name);
		int chiinoAddr = ialloc();      //创建inode节点
		if (chiinoAddr == -1)
		{
			printf("分配失败\n");
			return false;
		}
		dirlist[posj].inodeAddr = chiinoAddr;  //分配inode节点
		Inode p;
		p.i_ino = (chiinoAddr - Inode_StartAddr) / superblock->s_INODE_SIZE;
		p.i_atime = time(NULL);
		p.i_ctime = time(NULL);
		p.i_mtime = time(NULL);
		p.i_cnt = 1;


		Inode cur;
		fseek(fr, parinoAddr, SEEK_SET);
		fread(&cur, sizeof(Inode), 1, fr); //取出inode
		int len = cur.i_size;
		if (cur.i_size % 1024 != 0)
		{
			len = ((cur.i_size / 1024) + 1) * 1024;
		}
		p.i_size = len;
		char *file_content = new char[len];  //创建空间
		char *mov_ptr = file_content;      //移动指针
		for (int i = 0; i < 10; i++)    //读取直接块
		{
			if (cur.i_dirBlock[i] == 0)
				break;
			fseek(fr, cur.i_dirBlock[i], SEEK_SET);
			fread(mov_ptr, 1024, 1, fr);
			mov_ptr += 1024;
		}

		if (cur.i_indirBlock_1 != 0)   //读取一级间接块
		{
			int* tmp = new int[256];
			fseek(fr, cur.i_indirBlock_1, SEEK_SET);
			fread(tmp, 1024, 1, fr);
			for (int i = 0; i < 256; i++)
			{
				if (tmp[i] == 0)
					break;
				fseek(fr, tmp[i], SEEK_SET);
				fread(mov_ptr, 1024, 1, fr);
				mov_ptr += 1024;
			}
			delete tmp;
		}
		if (cur.i_indirBlock_2 != 0)  //读取二级间接块
		{
			int* tmp = new int[256];
			fseek(fr, cur.i_indirBlock_2, SEEK_SET);
			fread(tmp, 1024, 1, fr);
			for (int i = 0; i < 256; i++)
			{
				if (tmp[i] == 0)
					break;
				int *tmp_1 = new int[256];
				fseek(fr, tmp[i], SEEK_SET);
				fread(tmp_1, 1024, 1, fr);
				for (int j = 0; j < 256; j++)
				{
					if (tmp_1[j] == 0)
						break;
					fseek(fr, tmp_1[j], SEEK_SET);
					fread(mov_ptr, 1024, 1, fr);
					mov_ptr += 1024;
				}
				delete tmp_1;
			}
			delete tmp;
		}
		if (cur.i_indirBlock_3 != 0)  //读取三级间接块
		{
			int* tmp = new int[256];
			fseek(fr, cur.i_indirBlock_3, SEEK_SET);
			fread(tmp, 1024, 1, fr);
			for (int i = 0; i < 256; i++)
			{
				if (tmp[i] == 0)
					break;
				int* tmp_1 = new int[256];
				fseek(fr, tmp[i], SEEK_SET);
				fread(tmp_1, 1024, 1, fr);
				for (int j = 0; j < 256; j++)
				{
					if (tmp_1[j] == 0)
						break;
					int* tmp_2 = new int[256];
					fseek(fr, tmp_1[j], SEEK_SET);
					fread(tmp_2, 1024, 1, fr);
					for (int p = 0; p < 256; p++)
					{
						if (tmp_2[p] == 0)
							break;
						fseek(fr, tmp_2[p], SEEK_SET);
						fread(mov_ptr, 1024, 1, fr);
						mov_ptr += 1024;
					}
					delete tmp_2;
				}
				delete tmp_1;
			}
			delete tmp;
		}

		int storage = 0;   //已储存大小

		for (int i = 0; i < 10; i++)   //分配直接块
		{
			p.i_dirBlock[i] = balloc();
			fseek(fw, p.i_dirBlock[i], SEEK_SET);
			fread(file_content + storage, 1024, 1, fw);
			storage += 1024;
			if (storage >= len)
				break;
		}

		if (storage >= len)
		{
			delete file_content;
			return true;
		}

		p.i_indirBlock_1 = balloc();    //分配一级间接块
		int *tmp = new int[256];
		for (int i = 0; i < 256; i++)
		{
			tmp[i] = balloc();
			fseek(fw, tmp[i], SEEK_SET);
			fwrite(file_content + storage, 1024, 1, fw);
			storage += 1024;
			if (storage >= len)
				break;
		}
		fseek(fw, p.i_indirBlock_1, SEEK_SET);
		fwrite(tmp, 1024, 1, fw);
		delete tmp;

		if (storage >= len)
		{
			delete file_content;
			return true;
		}

		p.i_indirBlock_2 = balloc(); //分配二级间接块
		int* tmp = new int[256];
		for (int i = 0; i < 256; i++)
		{
			int* tmp_1 = new int[256];
			for (int j = 0; j < 256; j++)
			{
				tmp_1[j] = balloc();
				fseek(fw, tmp_1[j], SEEK_SET);
				fwrite(file_content + storage, 1024, 1, fw);
				storage += 1024;
				if (storage >= len)
					break;
			}
			fseek(fw, tmp[i], SEEK_SET);
			fwrite(tmp_1, 1024, 1, fw);
			delete tmp_1;
			if (storage >= len)
				break;
		}
		fseek(fw, p.i_indirBlock_1, SEEK_SET);
		fwrite(tmp, 1024, 1, fw);
		delete tmp;

		if (storage >= len)
		{
			delete file_content;
			return true;
		}

		p.i_indirBlock_3 = balloc();//分配三级间接块
		int* tmp = new int[256];
		for (int i = 0; i < 256; i++)
		{
			int* tmp_1 = new int[256];
			for (int j = 0; j < 256; j++)
			{
				int* tmp_2 = new int[256];
				for (int p = 0; p < 256; p++)
				{
					tmp_2[p] = balloc();
					fseek(fw, tmp_2[p], SEEK_SET);
					fwrite(file_content + storage, 1024, 1, fw);
					storage += 1024;
					if (storage >= len)
						break;
				}
				fseek(fw, tmp_1[j], SEEK_SET);
				fwrite(tmp_2, 1024, 1, fw);
				delete tmp_2;
				if (storage >= len)
					break;
			}
			fseek(fw, tmp[i], SEEK_SET);
			fwrite(tmp_1, 1024, 1, fw);
			delete tmp_1;
			if (storage >= len)
				break;
		}
		fseek(fw, p.i_indirBlock_3, SEEK_SET);
		fwrite(tmp, 1024, 1, fw);
		delete tmp;

		delete file_content;
		if (storage < len)
		{
			printf("文件过长\n");
		}
		p.i_mode = 0;
		fseek(fw, chiinoAddr, SEEK_SET);
		fwrite(&p, sizeof(Inode), 1, fw);

		fseek(fw, cur.i_dirBlock[posi], SEEK_SET);  //写回目录磁盘块
		fwrite(dirlist, sizeof(dirlist), 1, fw);

		cur.i_cnt++;
		fseek(fw, parinoAddr, SEEK_SET);
		fwrite(&cur, sizeof(Inode), 1, fw);
		fflush(fw);
		return true;




	}
	return false;
}*/

void FileSystem::more(int parinoAddr)//分页显示
{
	Inode cur;
	fseek(fr, parinoAddr, SEEK_SET);
	fread(&cur, sizeof(Inode), 1, fr); //取出inode
	int len = cur.i_size;
	if (cur.i_size % 1024 != 0)
	{
		len = ((cur.i_size / 1024) + 1) * 1024;
	}
	char *file_content = new char[len];  //创建空间
	char *mov_ptr = file_content;      //移动指针
	for (int i = 0; i < 10; i++)    //读取直接块
	{
		if (cur.i_direct_block[i] == 0)
			break;
		fseek(fr, cur.i_direct_block[i], SEEK_SET);
		fread(mov_ptr, 1024, 1, fr);
		mov_ptr += 1024;
	}

	if (cur.i_indirect_block_1 != 0)   //读取一级间接块
	{
		int* tmp = new int[256];
		fseek(fr, cur.i_indirect_block_1, SEEK_SET);
		fread(tmp, 1024, 1, fr);
		for (int i = 0; i < 256; i++)
		{
			if (tmp[i] == 0)
				break;
			fseek(fr, tmp[i], SEEK_SET);
			fread(mov_ptr, 1024, 1, fr);
			mov_ptr += 1024;
		}
		delete tmp;
	}
	if (cur.i_indirect_block_2 != 0)  //读取二级间接块
	{
		int* tmp = new int[256];
		fseek(fr, cur.i_indirect_block_2, SEEK_SET);
		fread(tmp, 1024, 1, fr);
		for (int i = 0; i < 256; i++)
		{
			if (tmp[i] == 0)
				break;
			int *tmp_1 = new int[256];
			fseek(fr, tmp[i], SEEK_SET);
			fread(tmp_1, 1024, 1, fr);
			for (int j = 0; j < 256; j++)
			{
				if (tmp_1[j] == 0)
					break;
				fseek(fr, tmp_1[j], SEEK_SET);
				fread(mov_ptr, 1024, 1, fr);
				mov_ptr += 1024;
			}
			delete tmp_1;
		}
		delete tmp;
	}
	if (cur.i_indirect_block_3 != 0)  //读取三级间接块
	{
		int* tmp = new int[256];
		fseek(fr, cur.i_indirect_block_3, SEEK_SET);
		fread(tmp, 1024, 1, fr);
		for (int i = 0; i < 256; i++)
		{
			if (tmp[i] == 0)
				break;
			int* tmp_1 = new int[256];
			fseek(fr, tmp[i], SEEK_SET);
			fread(tmp_1, 1024, 1, fr);
			for (int j = 0; j < 256; j++)
			{
				if (tmp_1[j] == 0)
					break;
				int* tmp_2 = new int[256];
				fseek(fr, tmp_1[j], SEEK_SET);
				fread(tmp_2, 1024, 1, fr);
				for (int p = 0; p < 256; p++)
				{
					if (tmp_2[p] == 0)
						break;
					fseek(fr, tmp_2[p], SEEK_SET);
					fread(mov_ptr, 1024, 1, fr);
					mov_ptr += 1024;
				}
				delete tmp_2;
			}
			delete tmp_1;
		}
		delete tmp;
	}
	int h, count;
	char a;
	for (h = 0; h < strlen(file_content); h++)
	{
		if (file_content[h] == '\n')
			count++;
		if (count % 24 == 0)
		{
			//scanf("%c", &a);
			system("pause");
			system("cls");
		}
		printf("%c",file_content[h]);
	}
	delete file_content;
}

void FileSystem::att(int parinoAddr,const char *file)//显示空间文件属性
{/*
	std::string fi = file;
	printf("名字");
	printf("\tInode");
	printf("\t文件");
	printf("\t文件数");
	printf("\t文件大小");
	printf("\t创建时间");
	printf("\t修改时间");
	printf("\t打开时间");
	printf("\n");
	std::string pri;
	for (int i = 0; i < fi.length(); i++)
	{
		if (fi[0] == ' ')
		{
			int posi = -1, posj = -1;
			DirectoryLookup(parinoAddr, file, TYPE_DIR, posi, posj);
			pri = "";
		}
		else
		{
			pri[pri.length() - 1] = fi[i];
		}
	}
	return;
	*/
}

void FileSystem::type_txt(int parinoAddr)  //显示当前文件夹下的文本文件
{
	Inode cur;
	fseek(fr, parinoAddr, SEEK_SET);
	fread(&cur, sizeof(Inode), 1, fr); //取出inode
	int len = cur.i_size;
	if (cur.i_size % 1024 != 0)
	{
		len = ((cur.i_size / 1024) + 1) * 1024;
	}
	char *file_content = new char[len];  //创建空间
	char *mov_ptr = file_content;      //移动指针
	for (int i = 0; i < 10; i++)    //读取直接块
	{
		if (cur.i_direct_block[i] == 0)
			break;
		fseek(fr, cur.i_direct_block[i], SEEK_SET);
		fread(mov_ptr, 1024, 1, fr);
		mov_ptr += 1024;
	}

	if (cur.i_indirect_block_1 != 0)   //读取一级间接块
	{
		int* tmp = new int[256];
		fseek(fr, cur.i_indirect_block_1, SEEK_SET);
		fread(tmp, 1024, 1, fr);
		for (int i = 0; i < 256; i++)
		{
			if (tmp[i] == 0)
				break;
			fseek(fr, tmp[i], SEEK_SET);
			fread(mov_ptr, 1024, 1, fr);
			mov_ptr += 1024;
		}
		delete tmp;
	}
	if (cur.i_indirect_block_2 != 0)  //读取二级间接块
	{
		int* tmp = new int[256];
		fseek(fr, cur.i_indirect_block_2, SEEK_SET);
		fread(tmp, 1024, 1, fr);
		for (int i = 0; i < 256; i++)
		{
			if (tmp[i] == 0)
				break;
			int *tmp_1 = new int[256];
			fseek(fr, tmp[i], SEEK_SET);
			fread(tmp_1, 1024, 1, fr);
			for (int j = 0; j < 256; j++)
			{
				if (tmp_1[j] == 0)
					break;
				fseek(fr, tmp_1[j], SEEK_SET);
				fread(mov_ptr, 1024, 1, fr);
				mov_ptr += 1024;
			}
			delete tmp_1;
		}
		delete tmp;
	}
	if (cur.i_indirect_block_3 != 0)  //读取三级间接块
	{
		int* tmp = new int[256];
		fseek(fr, cur.i_indirect_block_3, SEEK_SET);
		fread(tmp, 1024, 1, fr);
		for (int i = 0; i < 256; i++)
		{
			if (tmp[i] == 0)
				break;
			int* tmp_1 = new int[256];
			fseek(fr, tmp[i], SEEK_SET);
			fread(tmp_1, 1024, 1, fr);
			for (int j = 0; j < 256; j++)
			{
				if (tmp_1[j] == 0)
					break;
				int* tmp_2 = new int[256];
				fseek(fr, tmp_1[j], SEEK_SET);
				fread(tmp_2, 1024, 1, fr);
				for (int p = 0; p < 256; p++)
				{
					if (tmp_2[p] == 0)
						break;
					fseek(fr, tmp_2[p], SEEK_SET);
					fread(mov_ptr, 1024, 1, fr);
					mov_ptr += 1024;
				}
				delete tmp_2;
			}
			delete tmp_1;
		}
		delete tmp;
	}
	printf("%s", file_content);
	delete file_content;
	return;
}

void FileSystem::_Find(const char *a, int parinoAddr, const char *b) 
{
	Inode file;//当前文件
	std::string s = b;//拷贝当前目录字符串
	std::string s1;
	FILE *fp = fopen(FILESYSNAME, "rb");
	fseek(fp, parinoAddr, SEEK_SET);
	fread(&file, sizeof(Inode), 1, fp);
	int i = 0;
	while (i < 320) {//遍历十个磁盘块
		DirItem dirlist[32] = { 0 };//对应每个磁盘块中的32个目录项
		if (file.i_direct_block[i / 32] == -1) {
			i += 32;
			continue;//未使用则跳过
		}
		int parblockAddr = file.i_direct_block[i / 32];//第i/32号磁盘块
		fseek(fp, parblockAddr, SEEK_SET);
		fread(&dirlist, sizeof(dirlist), 1, fp);//将该磁盘块中的内容读入dirlist中，从而可以对该磁盘块中可能存在的目录项进行操作
		int j;
		for (j = 0; j < 32; j++) {//从0到31，对应每个目录项
			Inode tmp;//当前遍历到的目录项所对应的inode
			fseek(fp, dirlist[j].inode_address, SEEK_SET);//确定第j个目录项的inode位置
			fread(&tmp, sizeof(Inode), 1, fp);//读取该inode
			if (tmp.i_type == TYPE_DIR) // 如果为文件
			{
				if (strstr(dirlist[j].itemName, a) != NULL) {
					s1 = s;
					s1 = s1 + dirlist[j].itemName;
					printf("%s", s1.c_str());//path_output(parinoAddr, dirlist[j].itemName);//名称相同则输出
				}
			}
			else {//如果为文件夹
				if (strstr(dirlist[j].itemName, a) != NULL) {
					s1 = s;
					s1 = s1 + dirlist[j].itemName + '/';
					printf("%s", s1.c_str());//path_output(parinoAddr,dirlist[j].itemName);//名称相同则输出
				}
				_Find(a, dirlist[j].inode_address, s1.c_str());//继续寻找
			}
			i++;
		}
	}
}

void FileSystem::find(const char *a)//寻找文件
{
	std::string b = "/";
	_Find(a, root_directory_address_, b.c_str());
}

