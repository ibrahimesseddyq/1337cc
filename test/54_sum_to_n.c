int printf(char *fmt, ...);

int sum_to_n(int n)
{
    int total;
    int i;
    total = 0;
    for (i = 1; i <= n; i = i + 1)
    {
        total = total + i;
    }
    return total;
}

int main()
{
    printf("%d\n", sum_to_n(10));
    return 0;
}
