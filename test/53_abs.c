int printf(char *fmt, ...);

int my_abs(int x)
{
    if (x < 0)
    {
        return 0 - x;
    }
    return x;
}

int main()
{
    printf("%d\n", my_abs(42));
    return 0;
}
