int printf(char *fmt, ...);

int lnot(int x)
{
    if (x == 0)
    {
        return 1;
    }
    return 0;
}

int main()
{
    printf("%d\n", lnot(0));
    printf("%d\n", lnot(1));
    return 0;
}
