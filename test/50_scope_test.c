int printf(char *fmt, ...);

int main()
{
    int x;
    x = 10;
    {
        int x;
        x = 20;
    }
    printf("%d\n", x);
    return 0;
}
