/* Point d'entree des tests du protocole sur Linux (Pi ou PC). */
int test_protocole_executer(void);

int main(void)
{
    return test_protocole_executer() == 0 ? 0 : 1;
}
