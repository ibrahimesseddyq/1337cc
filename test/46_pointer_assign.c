int printf(char *fmt, ...);

int global_x = 1;

void set_global(int v)
{
    global_x = v;
}

int main()
{
    set_global(99);
    printf("%d\n", global_x);
    return 0;
}
