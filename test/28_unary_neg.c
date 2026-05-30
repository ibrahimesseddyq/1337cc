int printf(char *fmt, ...);

int negate(int x)
{
    return 0 - x;
}

int main()
{
    printf("%d\n", negate(5));
    return 0;
}
