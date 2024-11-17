#define MAX 1024
typedef struct node
{
  char username[MAX];
  char pass[MAX];
  char folder[MAX];
  struct node *next;
} node_a;

node_a *CreateNode(char *username, char *pass, char *folder){
    node_a *temp = malloc(sizeof(node_a)); 
    temp->next = NULL;// Cho next trỏ tới NULL
    strcpy(temp->username,username);
    strcpy(temp->pass,pass);
    strcpy(temp->folder,folder); // Gán giá trị cho Node
    return temp;//Trả về node mới đã có giá trị
}

node_a *AddTail(node_a *head, char *username, char *pass, char *folder){
    node_a *temp, *p;// Khai báo 2 node tạm temp và p
    temp = CreateNode(username,pass,folder);//Gọi hàm createNode để khởi tạo node temp có next trỏ tới NULL và giá trị là value
    if(head == NULL){
        head = temp;     //Nếu linked list đang trống thì Node temp là head luôn
    }
    else{
        p = head;// Khởi tạo p trỏ tới head
        while(p->next != NULL){
            p = p->next;//Duyệt danh sách liên kết đến cuối. Node cuối là node có next = NULL
        }
        p->next = temp;//Gán next của thằng cuối = temp. Khi đó temp sẽ là thằng cuối(temp->next = NULL mà)
    }
    return head;
}
 
// linked list -> file
void saveData(node_a *head, char *filename){
	FILE *f;
	f = fopen(filename,"w");
	node_a *current;
	for (current = head; current; current = current->next)
		fprintf(f, "%s %s %s\n", current->username, current->pass,current->folder);
	fclose(f);
}
//find account
node_a *findNode(node_a *head, char *username){
	node_a *current = head;
	while(current != NULL){
		if (0 == strcmp(current->username, username))
			return current;
		current = current->next;
	}

	return NULL;
}

node_a *loadData(char *filename){ 
  int count =0;
	FILE *f;
	char username[MAX], pass[MAX], folder[MAX];
	node_a *head, *current;
	head = current = NULL;

	printf("Loading data...\n");
	// open file
	if((f = fopen(filename,"r"))==NULL){
		printf("Failed to open file");
		exit(0);
	}

	//data -> linked list
	while(fscanf(f,"%s %s %s\n", username,pass,folder) != EOF){
		node_a *node = malloc(sizeof(node_a));
		strcpy(node->username, username);
		strcpy(node->pass,pass);
    strcpy(node->folder,folder);

		if(head == NULL)
			current = head = node;
		else
			current = current->next = node;
		count++;
    // free(node);
	}

	fclose(f);
  
	printf("Successfully loaded %d account(s)\n",count);
	return head;
}

node_a *deleteHead(node_a *head){
	node_a *p = head;
	p = p->next;
	// delete(l);
	return p;
}

node_a *deleteAt(node_a *head, char *username){
	node_a *current = head;
	node_a *next ;
	while(current != NULL){
		next = current->next;
		if (0 == strcmp(next->username, username))
			break;
		current = current->next;
	}
	current->next = current->next->next;
	
	return head;
}

node_a *updateNode(node_a *head,node_a *found){
	char pass[MAX],folder[MAX];
	printf("New Password: ");
	fgets(pass,MAX,stdin);
	pass[strcspn(pass,"\n")] = '\0';
	strcpy(found->pass,pass);
	int c = 0;
	do
	{
		printf("New Folder Name: ");
		fgets(folder,MAX,stdin);
		folder[strcspn(folder,"\n")] = '\0';

		errno = 0;
		int ret = mkdir(folder, S_IRWXU);
		if (ret == -1) {
			switch (errno) {
				case EACCES :
					printf("The root directory does not allow write. ");
					break;
				case EEXIST:
					printf("\nFolder %s already exists. \n\"%s\" used for %s\n",folder, folder,found->username);
					c = 1;
					break;
				case ENAMETOOLONG:
					printf("Pathname is too long");
					break;
				default:
					printf("mkdir");
					break;
			}
		}
		else
		{
		printf("Created: %s\n", folder);
		printf("Folder %s is created\n",folder);
		c = 1;
		}
	} while (c == 0);
	strcpy(found->folder,folder);
	return head;
}