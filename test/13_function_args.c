int printf(char *fmt, ...);

int max(int a, int b)
{
    if (a > b)
    {
        return a;
    }
    return b;
}

int main()
{
    printf("%d\n", max(3, 7));
    return 0;
}
