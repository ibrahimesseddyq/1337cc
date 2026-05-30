int printf(char *fmt, ...);

int main()
{
    int i;
    int j;
    for (i = 0; i < 3; i = i + 1)
    {
        for (j = 0; j < 3; j = j + 1)
        {
            printf("%d\n", i * 3 + j + 1);
        }
    }
    return 0;
}
