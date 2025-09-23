truct book
{
    char name[30];
};

int test(int a,char* fmt, int b)
{
    int i = 5;
    return i;
}

struct book book[3];

int main() {
    struct book* books;
    return test(56, books[0].name, 1000);
}
