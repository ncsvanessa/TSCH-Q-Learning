# TSCH-Q-Learning com Aprendizado Federado

Implementação de Q-Learning **com Aprendizado Federado** para otimização de escalonamento em redes TSCH (Time Slotted Channel Hopping) usando Contiki-NG.

# Descrição

Este projeto implementa um algoritmo de aprendizado por reforço (Q-Learning) **combinado com Aprendizado Federado** para otimizar o escalonamento de slots de tempo em redes TSCH. Os nós da rede **compartilham e agregam** seus conhecimentos (Q-tables) para acelerar o aprendizado coletivo e melhorar a eficiência global da rede.

O sistema aprende dinamicamente a melhor alocação de slots baseando-se em métricas de desempenho como:

- Taxas de transmissão e recepção bem-sucedidas
- Gerenciamento de buffer
- Detecção e penalização de conflitos
- Throughput da rede
- **Conhecimento agregado de múltiplos nós (Federated Learning)**

# Estrutura do Projeto

```
TSCH-Q-Learning/
├── examples/           # Código de exemplo e configurações
│   ├── node.c         # Implementação do nó sensor com FL
│   ├── Makefile       # Build system
│   └── project-conf.h # Configurações do projeto
├── net/               # Estruturas de rede
│   ├── queuebuf.c    # Gerenciamento de buffer de pacotes
│   └── queuebuf.h
├── tsch/              # Módulos TSCH, Q-Learning e Federated Learning
│   ├── q-learning.c          # Implementação do algoritmo Q-Learning
│   ├── q-learning.h
│   ├── federated-learning.c  # Implementação do Aprendizado Federado
│   ├── federated-learning.h
│   ├── customized-tsch-file.c # Extensões TSCH customizadas
│   ├── customized-tsch-file.h
│   ├── tsch-slot-operation.c  # Operações de slots TSCH
│   ├── tsch-slot-operation.h
│   └── tsch.h
└── logs/              # Logs de execução
    └── loglistener_qlearning-05-12.txt
```

# Características

## Aprendizado Federado (Federated Learning)
- **Comunicação P2P**: Nós compartilham Q-tables via UDP (porta 8766)
- **Métodos de Agregação**:
  - **FedAvg**: Média simples das Q-tables
  - **Weighted FedAvg**: Média ponderada baseada em experiência (padrão)
  - **FedMedian**: Mediana robusta a outliers
- **Sincronização**: A cada 180 segundos (configurável)
- **Máximo de Vizinhos**: 10 nós (configurável)
- **Limpeza Automática**: Remove vizinhos inativos após timeout
- **Balanceamento Local/Global**: Peso configurável entre modelo local e federado

## Q-Learning
- Tabela Q com tamanho configurável (padrão: tamanho do slotframe)
- Taxa de aprendizado (learning rate): 0.1
- Fator de desconto (discount factor): 0.9
- Função de recompensa baseada em:
  - θ₁ = 3.0 (peso para transmissões bem-sucedidas)
  - θ₂ = 1.5 (peso para gerenciamento de buffer)
  - θ₃ = 0.5 (peso para conflitos)
  - Penalidade de conflito = 100.0

## TSCH
- Escalonamento dinâmico de slots
- Detecção de conflitos
- Suporte para múltiplos tipos de dados (UNICAST, BROADCAST, EB)
- Fila de status de pacotes customizada

# Configuração

## Pré-requisitos

- Contiki-NG
- Compilador GCC para arquitetura alvo
- Make

## Parâmetros Configuráveis

### Q-Learning
```c
// Tamanho da tabela Q-value
#define Q_VALUE_LIST_SIZE TSCH_SCHEDULE_DEFAULT_LENGTH

// Tamanho da fila de transmissão
#define MAX_NUMBER_OF_CUSTOM_QUEUE 20

// Habilitar impressão de registros
#define PRINT_TRANSMISSION_RECORDS 1
```

### Aprendizado Federado
```c
// Habilitar/desabilitar aprendizado federado
#define ENABLE_FEDERATED_LEARNING 1

// Número máximo de vizinhos
#define MAX_FEDERATED_NEIGHBORS 10

// Intervalo de sincronização (segundos)
#define FEDERATED_SYNC_INTERVAL 180

// Métodos disponíveis: FEDAVG, WEIGHTED_FEDAVG, FEDMEDIAN
federated_learning_init(WEIGHTED_FEDAVG);

// Peso do modelo local (0.0 a 1.0)
set_local_model_weight(0.5);  // 50% local, 50% federado
```

# Compilação e Execução

## Compilar o Projeto

```bash
cd examples/
make TARGET=<seu-target>
```

### Upload para o Hardware

```bash
make TARGET=<seu-target> node.upload
```

## Executar no Cooja Simulator

1. Abra o Cooja
2. Crie uma nova simulação
3. Adicione nós com o firmware compilado
4. Configure a rede TSCH
5. Inicie a simulação

# Função de Recompensa

A função de recompensa TSCH é calculada como:

```
R = θ₁ × (n_tx + n_rx) - θ₂ × buffer_penalty - conflict_penalty × n_conflicts
```

Onde:
- `n_tx`: número de transmissões bem-sucedidas
- `n_rx`: número de recepções bem-sucedidas
- `buffer_penalty`: penalidade baseada no tamanho do buffer
- `n_conflicts`: número de conflitos detectados

# Monitoramento

Os logs são gerados em tempo de execução e incluem:
- Registros de transmissão/recepção com números de slot
- Atualização da tabela Q
- Detecção de conflitos
- Status do buffer
- **📡 Broadcast e recepção de Q-tables**
- **🔗 Agregação federada com estatísticas**
- **👥 Número de vizinhos ativos**
- **⚖️ Método de agregação utilizado**

Os logs são salvos na pasta `logs/`.

### Exemplo de Log Federado
```
[INFO: FedLearn] Federated Learning initialized with method=1
[INFO: FedLearn] Received Q-table from node 3 (samples=15)
[INFO: FedLearn] Broadcasting Q-table (samples=12)
[INFO: FedLearn] Weighted FedAvg: local_weight=0.44, neighbors=2
[INFO: FedLearn] Federated aggregation complete: neighbors=2, method=1, local_samples=12
```

# Processos

## UDP Communication Process
- Porta UDP: 8765 (dados de aplicação)
- Intervalo de envio: 60 segundos
- Gerencia comunicação entre nós

## RL-TSCH Scheduler Process
- Intervalo de atualização da tabela Q: 120 segundos
- Setup do escalonamento mínimo: 120 segundos
- Implementa o algoritmo de aprendizado

## Federated Learning Sync Process ⭐ NOVO
- Porta UDP: 8766 (compartilhamento de Q-tables)
- Intervalo de sincronização: 180 segundos
- Broadcast de Q-tables locais
- Agregação de conhecimento de vizinhos
- Limpeza de entradas obsoletas

# Estruturas de Dados

## `env_state`
Armazena o estado do ambiente:
- `buffer_size`: tamanho do buffer atual
- `energy_level`: nível de energia do nó

## `packet_status`
Rastreia status de transmissão de pacotes:
- `data_type`: tipo de dados (UNICAST, BROADCAST, EB)
- `packet_seqno`: número de sequência
- `transmission_count`: contagem de transmissões
- `time_slot`: slot de tempo usado
- `channel_offset`: offset do canal
- `node_id`: ID do nó
- `trans_addr`: endereço de transmissão

# API Principal

## Aprendizado Federado
```c
// Inicializar sistema federado
void federated_learning_init(fed_aggregation_method_t method);

// Armazenar Q-table de vizinho
uint8_t store_neighbor_q_table(uint16_t node_id, float *q_values, uint8_t num_samples);

// Agregar Q-tables (chama método configurado)
uint8_t federated_aggregate(void);

// Agregação FedAvg (média simples)
uint8_t federated_aggregate_fedavg(void);

// Agregação ponderada por experiência
uint8_t federated_aggregate_weighted(void);

// Agregação por mediana (robusta)
uint8_t federated_aggregate_median(void);

// Obter Q-table local para compartilhar
float* get_local_q_table_for_sharing(void);

// Incrementar contador de amostras locais
void increment_local_samples(void);

// Limpar vizinhos obsoletos
void cleanup_stale_neighbors(uint32_t timeout_seconds);

// Configurar método de agregação
void set_aggregation_method(fed_aggregation_method_t method);

// Configurar peso do modelo local (0.0 - 1.0)
void set_local_model_weight(float weight);

// Obter estatísticas federadas
void get_federated_stats(uint8_t *num_neighbors, uint8_t *local_samples, 
                         fed_aggregation_method_t *method);
```

## Q-Learning
```c
// Retorna a ação com maior Q-value
uint8_t get_highest_q_val(void);

// Atualiza a tabela Q
void update_q_table(uint8_t action, float got_reward);

// Calcula recompensa TSCH
float tsch_reward_function(uint8_t n_tx, uint8_t n_rx, 
                          uint8_t n_buff_prev, 
                          uint8_t n_buff_new, 
                          uint8_t n_conflicts);
```

## Gerenciamento de Fila
```c
// Adiciona pacote à fila
void enqueue(queue_packet_status *queue, packet_status pkt_sts);

// Verifica se fila está vazia/cheia
int isEmpty(queue_packet_status *queue);
int isFull(queue_packet_status *queue);
```

# Desempenho

O algoritmo Q-Learning **com Aprendizado Federado** aprende continuamente para:
- ✅ Maximizar throughput
- ✅ Minimizar conflitos de slot
- ✅ Otimizar uso do buffer
- ✅ Melhorar eficiência energética
- ✅ **Acelerar convergência através de conhecimento compartilhado**
- ✅ **Melhorar robustez com agregação de múltiplos nós**
- ✅ **Adaptar-se mais rápido a mudanças na topologia da rede**

## Benefícios do Aprendizado Federado

### 🚀 Convergência Mais Rápida
Cada nó aprende não apenas com suas próprias experiências, mas também com as experiências de seus vizinhos, acelerando significativamente o processo de aprendizado.

### 🛡️ Maior Robustez
A agregação de múltiplas Q-tables reduz o impacto de experiências atípicas ou ruído em nós individuais.

### 🔄 Adaptação Dinâmica
A rede se adapta melhor a mudanças topológicas, pois o conhecimento é distribuído e continuamente atualizado.

### 📊 Privacidade Preservada
Apenas as Q-tables são compartilhadas, não os dados brutos dos pacotes ou informações sensíveis.

### ⚖️ Balanceamento Configurável
O parâmetro `aggregation_weight` permite ajustar o equilíbrio entre conhecimento local e federado.

# Contribuindo

Contribuições são bem-vindas! Sinta-se à vontade para:
- Reportar bugs
- Sugerir novas funcionalidades
- Melhorar documentação
- Submeter pull requests

# Licença

Este projeto é fornecido para fins educacionais e de pesquisa.

# Autores

Desenvolvido como parte de pesquisa em otimização de redes TSCH usando aprendizado por reforço.

# Referências

- Contiki-NG: https://github.com/contiki-ng/contiki-ng
- TSCH: IEEE 802.15.4e Time Slotted Channel Hopping
- Q-Learning: Sutton & Barto - Reinforcement Learning
- **Federated Learning: McMahan et al. - Communication-Efficient Learning of Deep Networks from Decentralized Data (2017)**
- **FedAvg: Federated Averaging Algorithm**

## Artigos Relacionados
- **"Federated Reinforcement Learning for IoT Networks"**
- **"Distributed Q-Learning in Wireless Sensor Networks"**
- **"Privacy-Preserving Machine Learning in WSNs"**

---

**Nota**: Este é um projeto de pesquisa em desenvolvimento com suporte a **Aprendizado Federado**. Para uso em produção, testes adicionais e otimizações são recomendados.

## 🆕 Novidades da Versão Federada

### v2.0 - Aprendizado Federado
- ✅ Implementação completa de Federated Learning
- ✅ Três métodos de agregação (FedAvg, Weighted, Median)
- ✅ Comunicação UDP para compartilhamento de Q-tables
- ✅ Sistema de limpeza de vizinhos obsoletos
- ✅ Estatísticas detalhadas de agregação
- ✅ Configuração flexível de parâmetros
- ✅ Preservação de privacidade (apenas Q-tables compartilhadas)