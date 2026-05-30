int printf(char *fmt, ...);

int main()
{
    int i;
    for (i = 5; i >= 1; i = i - 1)
    {
        printf("%d\n", i);
    }
    return 0;
}
