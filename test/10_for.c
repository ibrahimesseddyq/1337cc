int printf(char *fmt, ...);

int main()
{
    int i;
    for (i = 0; i < 5; i = i + 1)
    {
        printf("%d\n", i);
    }
    return 0;
}
