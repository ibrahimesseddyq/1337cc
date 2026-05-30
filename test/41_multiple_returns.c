int printf(char *fmt, ...);

int check(int x)
{
    if (x > 100)
    {
        return 2;
    }
    if (x > 10)
    {
        return 1;
    }
    return 0;
}

int main()
{
    printf("%d\n", check(150));
    printf("%d\n", check(50));
    printf("%d\n", check(5));
    return 0;
}
