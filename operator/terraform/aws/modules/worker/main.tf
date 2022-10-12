/**
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

terraform {
  required_version = "~> 1.2.3"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 3.0"
    }
  }
}

locals {
  // how often to collect the metric in seconds
  metric_period = "60"
}

################################################################################
# EC2 Instances
################################################################################

resource "aws_key_pair" "authorized_key" {
  count      = var.worker_ssh_public_key != "" ? 1 : 0
  key_name   = "${var.environment}_ssh_public_key"
  public_key = var.worker_ssh_public_key
}

data "aws_ami" "worker_image" {
  most_recent = true
  owners      = var.ami_owners
  filter {
    name = "name"
    values = [
      // AMI name format {ami-name}--YYYY-MM-DD'T'hh-mm-ssZ
      "${var.ami_name}--*"
    ]
  }
  filter {
    name = "state"
    values = [
      "available"
    ]
  }
}

resource "aws_iam_instance_profile" "worker_profile" {
  name = "${var.ec2_iam_role_name}Profile"
  role = var.ec2_iam_role_name

  depends_on = [aws_iam_role.enclave_role]
}

resource "aws_launch_template" "worker_template" {
  name = var.worker_template_name

  // TODO: Explore this parameter
  //  cpu_options {
  //    core_count       = 4
  //    threads_per_core = 1
  //  }

  credit_specification {
    cpu_credits = "standard"
  }

  iam_instance_profile {
    name = aws_iam_instance_profile.worker_profile.name
  }

  image_id = data.aws_ami.worker_image.id

  instance_type = var.instance_type

  key_name = var.worker_ssh_public_key != "" ? aws_key_pair.authorized_key[0].key_name : null

  monitoring {
    enabled = true
  }

  enclave_options {
    enabled = true
  }

  tag_specifications {
    resource_type = "instance"
    tags = {
      Name               = "${var.service}-${var.environment}"
      service            = var.service
      environment        = var.environment
      enclave_cpu_count  = var.enclave_cpu_count
      enclave_memory_mib = var.enclave_memory_mib
    }
  }

  vpc_security_group_ids = var.enable_customized_vpc ? var.customized_vpc_security_group_ids : [aws_security_group.worker_sg[0].id]
}

################################################################################
# IAM Role
################################################################################

resource "aws_iam_role" "enclave_role" {
  name = var.ec2_iam_role_name

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Action = "sts:AssumeRole"
        Effect = "Allow"
        Sid    = ""
        Principal = {
          Service = "ec2.amazonaws.com"
        }
      },
    ]
  })

  inline_policy {
    name = "enclave_role"

    policy = jsonencode({
      Version = "2012-10-17"
      Statement = concat([
        {
          Effect = "Allow"
          Action = [
            "sts:AssumeRole"
          ]
          Resource = var.coordinator_a_assume_role_arn
        },
        {
          Effect = "Allow"
          Action = [
            "dynamodb:ConditionCheckItem",
            "dynamodb:GetItem",
            "dynamodb:PutItem"
          ]
          Resource = var.metadata_db_table_arn
        },
        {
          Effect = "Allow"
          Action = [
            "sqs:DeleteMessage",
            "sqs:ReceiveMessage",
            "sqs:SendMessage",
            "sqs:ChangeMessageVisibility"
          ]
          Resource = var.job_queue_arn
        },
        {
          Effect = "Allow"
          Action = [
            "s3:GetObject",
            "s3:PutObject",
            "s3:DeleteObject",
            "s3:ListBucket",
            "ec2:DescribeTags",
            "autoscaling:CompleteLifecycleAction",
            "autoscaling:DescribeAutoScalingInstances",
            "ssm:UpdateInstanceInformation",
            "ssm:ListInstanceAssociations"
          ]
          Resource = "*"
        },
        {
          Effect   = "Allow"
          Action   = "ssm:GetParameters"
          Resource = "arn:aws:ssm:*:*:parameter/*"
        },
        {
          "Action" : "cloudwatch:PutMetricData",
          "Effect" : "Allow",
          "Resource" : "*"
        },
        ],
        // to prevent duplicate statements
        (var.coordinator_a_assume_role_arn == var.coordinator_b_assume_role_arn)
        ? []
        : [{
          Effect = "Allow"
          Action = [
            "sts:AssumeRole"
          ]
          Resource = var.coordinator_b_assume_role_arn
      }, ])
    })
  }
}
