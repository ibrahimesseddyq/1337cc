int printf(char *fmt, ...);

int main()
{
    int x;
    int y;
    x = 5;
    y = x;
    x = x - 1;
    printf("%d\n", y);
    return 0;
}
