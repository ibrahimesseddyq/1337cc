int printf(char *fmt, ...);

int main()
{
    int i;
    i = 1;
    do
    {
        printf("%d\n", i);
        i = i + 1;
    }
    while (i <= 3);
    return 0;
}
