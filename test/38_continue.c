int printf(char *fmt, ...);

int main()
{
    int i;
    for (i = 0; i < 5; i = i + 1)
    {
        if (i == 2)
        {
            continue;
        }
        printf("%d\n", i);
    }
    return 0;
}
