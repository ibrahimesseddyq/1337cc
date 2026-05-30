int printf(char *fmt, ...);

int classify(int n)
{
    if (n > 0)
    {
        return 1;
    }
    else
    {
        if (n < 0)
        {
            return 2;
        }
        else
        {
            return 0;
        }
    }
}

int main()
{
    printf("%d\n", classify(5));
    printf("%d\n", classify(0));
    printf("%d\n", classify(3));
    return 0;
}
