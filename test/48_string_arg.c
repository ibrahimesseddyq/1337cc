int printf(char *fmt, ...);

void print_msg(char *msg)
{
    printf("%s\n", msg);
}

int main()
{
    print_msg("hello from function");
    return 0;
}
