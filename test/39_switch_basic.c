int printf(char *fmt, ...);

int main()
{
    int x;
    x = 2;
    switch (x)
    {
        case 1:
            printf("one\n");
            break;
        case 2:
            printf("two\n");
            break;
    }
    return 0;
}
